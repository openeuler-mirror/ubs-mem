/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-mem is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "RackMemShm.h"
#include <sys/mman.h>
#include <mutex>
#include "ubs_mem_def.h"
#include "ubs_mem.h"
#include "ShmMetaDataMgr.h"
#include "fork_resources.h"
#include "log.h"
#include "mxm_msg.h"
#include "obmm.h"
#include "rack_mem_fd_map.h"
#include "rack_mem_functions.h"
#include "rack_mem_lib_common.h"
#include "rack_mem_macros.h"
#include "shm_ipc_command.h"
#include "system_adapter.h"
#include "ubsm_ptracer.h"

namespace ock::mxmd {
using namespace ock::common;
using namespace ock::ubsm;
static std::mutex multiNodeMapTransactionMutex;

static uint32_t RegisterMappingNames(const std::string &name, size_t memberCount, std::vector<std::string> &names)
{
    if (memberCount == 0 || memberCount > MAX_MULTINODE_HOST_NUM) {
        DBG_LOGERROR("Invalid shared memory member count for mapping in progress, name=" << name
                                                                                         << ", count=" << memberCount);
        return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
    }
    names.reserve(memberCount);
    names.push_back(name);
    constexpr char hex[] = "0123456789abcdef";
    for (size_t index = 1; index < memberCount; ++index) {
        const auto count = memberCount - 1;
        const char suffix[] = {hex[count >> 4], hex[count & 15], hex[index >> 4], hex[index & 15], '\0'};
        names.emplace_back(name + suffix);
    }
    return ShmMetaDataMgr::GetInstance().AddMappingInProgressNames(names);
}
uint32_t RackMemShm::UbsMemShmCreate(const std::string &regionName, const std::string &name, uint64_t size,
                                     const std::string &baseNid, const SHMRegionDesc &shmRegion, uint64_t flags,
                                     mode_t mode)
{
    DBG_LOGDEBUG("Start to create share memory, region name=" << regionName << ", memory name=" << name
                                                              << ", size=" << size << ", node id" << baseNid
                                                              << ", flags=" << flags << ", mode=" << mode);
    auto ret = CheckRegionPar(baseNid, shmRegion);
    if (BresultFail(ret)) {
        DBG_LOGERROR("Base node id is not in regions, node id=" << baseNid);
        return ret;
    }
    ret = ShmIpcCommand::IpcCallShmCreate(regionName, name, size, baseNid, shmRegion, flags, mode);
    if (BresultFail(ret)) {
        DBG_LOGERROR("Ipc call create interface, ret=" << ret);
        return ret;
    }
    DBG_LOGDEBUG("Create share memory successfully, name=" << name);
    return UBSM_OK;
}

uint32_t RackMemShm::UbsMemShmCreateWithProvider(const ubs_mem_provider_t *srcLoc, const std::string &name,
                                                 uint64_t size, uint64_t flags, mode_t mode)
{
    DBG_LOGDEBUG("Start to create share memory with provider, memory name="
                 << name << ", size=" << size << ", flags=" << flags << ", mode=" << mode
                 << ", provider host_name=" << srcLoc->host_name << ", socket_id=" << srcLoc->socket_id
                 << ", numa_id=" << srcLoc->numa_id << ", port_id=" << srcLoc->port_id);
    auto ret = ShmIpcCommand::IpcCallShmCreateWithProvider(srcLoc->host_name, srcLoc->socket_id, srcLoc->numa_id,
                                                           srcLoc->port_id, name, size, flags, mode);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("Ipc call create with provider failed, ret=" << ret);
        return ret;
    }
    DBG_LOGDEBUG("Create share memory with provider successfully, name=" << name);
    return UBSM_OK;
}

uint32_t RackMemShm::UbsMemShmCreateMultiNodePool(const std::string &name, uint64_t size, mode_t mode, uint64_t flags,
                                                  uint32_t socketId, const std::vector<std::string> &hostNames,
                                                  const std::vector<uint64_t> &hostSizes)
{
    DBG_LOGDEBUG("Start to create multi-node shared memory, name=" << name << ", size=" << size << ", flags=" << flags
                                                                   << ", mode=" << mode << ", socketId=" << socketId
                                                                   << ", hostCount=" << hostNames.size());
    const auto ret =
        ShmIpcCommand::IpcCallShmCreateMultiNodePool(name, size, mode, flags, socketId, hostNames, hostSizes);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("IPC call to create multi-node shared memory failed, name=" << name << ", ret=" << ret);
        return ret;
    }
    DBG_LOGDEBUG("Create multi-node shared memory successfully, name=" << name);
    return UBSM_OK;
}

uint32_t RackMemShm::UbsMemShmDeleteMultiNodePool(const std::string &name)
{
    const auto ret = ShmIpcCommand::IpcCallShmDeleteMultiNodePool(name);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("IPC call to delete multi-node shared memory failed, name=" << name << ", ret=" << ret);
        return ret;
    }
    DBG_LOGDEBUG("Delete multi-node shared memory successfully, name=" << name);
    return UBSM_OK;
}

static void UnMapWhenError(void *startAddr, size_t totalSize, const std::string &name, ShmAppMetaData &shmAppMetaData)
{
    if (startAddr == nullptr) {
        DBG_LOGERROR("startAddr is nullptr, name=" << name);
        return;
    }
    if (totalSize == 0) {
        DBG_LOGERROR("totalSize is zero, name=" << name);
        return;
    }
    if (SystemAdapter::MemoryUnMap(startAddr, totalSize) == -1) {
        DBG_LOGERROR("Munmap failed, name=" << name << ", totalSize=" << totalSize);
    }
    // 这里unmmap不进行处理, 失败了靠巡检解决
    if (BresultFail(ShmIpcCommand::IpcCallShmUnMap(name))) {
        DBG_LOGERROR("IpcCallShmUnMap error " << name);
    }
}

static bool MapWithFixedFlags(int flags)
{
    return (flags & MAP_FIXED) == MAP_FIXED || (flags & MAP_FIXED_NOREPLACE) == MAP_FIXED_NOREPLACE;
}

uint32_t RackMemShmMmapParameterCheck(void *start, size_t mapSize, int prot, int flags, const std::string &name,
                                      off_t off)
{
    if (ShmMetaDataMgr::GetInstance().GetAddr(name) != nullptr) {
        // 同一个name只能map一次
        DBG_LOGERROR("shared memory name is repeated, name=" << name);
        return MXM_ERR_PARAM_INVALID;
    }
    if (mapSize == 0) {
        DBG_LOGERROR("Size is zero, name=" << name);
        return MXM_ERR_PARAM_INVALID;
    }
    if (off != 0) {
        DBG_LOGERROR("Offset is non-zero, name=" << name << ", offset=" << off);
        return MXM_ERR_PARAM_INVALID;
    }
    if ((prot != PROT_READ) && (prot != PROT_WRITE) && (prot != (PROT_READ | PROT_WRITE)) && prot != PROT_NONE) {
        DBG_LOGERROR("Prot is invalid, name=" << name << ", prot=" << prot);
        return MXM_ERR_PARAM_INVALID;
    }
    if (start == nullptr && MapWithFixedFlags(flags)) {
        DBG_LOGERROR("Invalid flags, start is nullptr, name=" << name << ", flags=" << flags);
        return MXM_ERR_PARAM_INVALID;
    }
    auto pStart = reinterpret_cast<uintptr_t>(start);
    if (UNLIKELY(mapSize > std::numeric_limits<uintptr_t>::max() - pStart)) {
        DBG_LOGERROR("Address region is overflow. mapSize=" << mapSize);
        return MXM_ERR_PARAM_INVALID;
    }
    if (start != nullptr && MapWithFixedFlags(flags) &&
        ShmMetaDataMgr::GetInstance().HasOverlapWithKnownVmas(pStart, pStart + mapSize)) {
        DBG_LOGERROR("Overlap with known vmas, name=" << name);
        return MXM_ERR_PARAM_INVALID;
    }
    return UBSM_OK;
}

uint32_t RackMemShmMmapByMemID(void *mmapAddr, size_t unitSize, uint64_t mmapCount, std::vector<uint64_t> &memIds,
                               ShmAppMetaData &shmAppMetaData, uint64_t open_flag, int oflag, int prot, int flags)
{
    if (mmapCount > memIds.size()) {
        DBG_LOGERROR("mmapCount=" << mmapCount << " larger than memIds size=" << memIds.size());
        return MXM_ERR_PARAM_INVALID;
    }
    shmAppMetaData.addrs.clear();
    shmAppMetaData.slices.clear();
    // Allocate before opening devices so allocation failures cannot strand an unrecorded fd.
    shmAppMetaData.slices.reserve(mmapCount);
    shmAppMetaData.addrs.reserve(mmapCount);
    uint32_t ret;
    off_t offset = 0;
    if ((open_flag & UBSM_FLAG_MMAP_HUGETLB_PMD) == UBSM_FLAG_MMAP_HUGETLB_PMD) {
        offset |= OBMM_MMAP_FLAG_HUGETLB_PMD;
    }
    for (uint64_t i = 0; i < mmapCount; i++) {
        const int fd = ObmmOpenInternal(memIds[i], open_flag, oflag);
        TemporaryObmmFd fdGuard(fd);
        DBG_LOGINFO("It is " << i << "th mmap, mem id=" << memIds[i] << ", fd=" << fd << ", open_flag=" << open_flag);
        if (fd < 0) {
            DBG_LOGERROR("Obmm open error fd is " << fd);
            return MXM_ERR_SHM_OBMM_OPEN;
        }
        auto startAddr = reinterpret_cast<void *>(reinterpret_cast<uint64_t>(mmapAddr) + i * unitSize);
        if (i == mmapCount - 1) {
            size_t tmpLength = shmAppMetaData.mapSize - (mmapCount - 1) * unitSize;
            ret = RackMemFdMap::GetInstance().MapForEachFd(startAddr, tmpLength, fd, prot, flags, offset);
            if (BresultFail(ret)) {
                DBG_LOGERROR("MapForEachFd failed: fd=" << fd << ", ret=" << ret);
                return ret;
            }
            shmAppMetaData.AddAddr(startAddr, tmpLength);
            shmAppMetaData.slices.push_back({memIds[i], startAddr, tmpLength, offset, open_flag, oflag});
            break;
        }
        ret = RackMemFdMap::GetInstance().MapForEachFd(startAddr, unitSize, fd, prot, flags, offset);
        if (BresultFail(ret)) {
            DBG_LOGERROR("MapForEachFd failed: fd=" << fd << ", ret=" << ret);
            return ret;
        }
        shmAppMetaData.AddAddr(startAddr, unitSize);
        shmAppMetaData.slices.push_back({memIds[i], startAddr, unitSize, offset, open_flag, oflag});
    }
    return UBSM_OK;
}

static inline ShmOwnStatus GetOwnStatus(int prot)
{
    switch (prot) {
        case PROT_NONE:
            return ShmOwnStatus ::UNACCESS;
        case PROT_READ:
            return ShmOwnStatus ::READ;
        case PROT_WRITE:
            return ShmOwnStatus ::WRITE;
        case PROT_READ | PROT_WRITE:
            return ShmOwnStatus ::READWRITE;
        default:
            return OWNBUFF;
    }
}

mode_t convertToFileMode(ShmOwnStatus status)
{
    mode_t mode = 0;
    switch (status) {
        case WRITE:
            mode = S_IWUSR;
            break;
        case READ:
            mode = S_IRUSR;
            break;
        case READWRITE:
            mode = S_IRUSR | S_IWUSR;
            break;
        default:
            mode = 0;
    }
    return mode;
}

int RackMemShm::GetPreAllocateAddressAligned(void *start, size_t length, int flags, void *&result, size_t align)
{
    void *mmapAddr = nullptr;
    if (start == nullptr) { // 未指定地址时，内部预先分配一段地址
        TP_TRACE_BEGIN(TP_UBSM_SHM_MMAP_ADDR);
        auto ret = RackMemFdMap::MemoryMapAligned(length, mmapAddr, align);
        TP_TRACE_END(TP_UBSM_SHM_MMAP_ADDR, ret);
        if (BresultFail(ret)) {
            DBG_LOGERROR("MemoryMapAligned failed, length is " << length);
            return MXM_ERR_MMAP_VA_FAILED;
        }
        result = mmapAddr;
        return MXM_OK;
    }
    bool isAlignedTo2M = reinterpret_cast<uintptr_t>(start) % HUGE_PAGES_2M == 0;
    if (!isAlignedTo2M) {
        DBG_LOGERROR("PMD mapping is not aligned to 2M");
        return MXM_ERR_PARAM_INVALID;
    }

    if (MapWithFixedFlags(flags)) {
        result = start;
        return MXM_OK;
    }

    // 指定地址，但是不带MAP_FIXED，先尝试能否匿名map这段地址，如果返回的地址是align对齐，就用这个地址，否则内部重新分配一段地址
    auto ret = RackMemFdMap::MapAnonymousMemory(start, length, mmapAddr);
    if (BresultFail(ret)) {
        DBG_LOGWARN("MapAnonymousMemory failed, length is " << length);
        return ret;
    }
    if (reinterpret_cast<uintptr_t>(mmapAddr) % align != 0) {
        (void)SystemAdapter::MemoryUnMap(mmapAddr, length);
        TP_TRACE_BEGIN(TP_UBSM_SHM_MMAP_ADDR);
        ret = RackMemFdMap::MemoryMapAligned(length, mmapAddr, align);
        TP_TRACE_END(TP_UBSM_SHM_MMAP_ADDR, ret);
        if (BresultFail(ret)) {
            DBG_LOGERROR("MemoryMapAligned failed, length is " << length);
            return MXM_ERR_MMAP_VA_FAILED;
        }
        result = mmapAddr;
        return MXM_OK;
    }
    result = mmapAddr;
    return MXM_OK;
}
// 前置有校验start为空时，flags一定不会有MAP_FIXED和MAP_FIXED_NOREPLACE
int RackMemShm::GetPreAllocateAddress(void *start, size_t length, int flags, void *&result)
{
    void *mmapAddr = nullptr;
    if (start == nullptr) { // 未指定地址时，内部预先分配一段地址
        TP_TRACE_BEGIN(TP_UBSM_SHM_MMAP_ADDR);
        auto ret = RackMemFdMap::MapAnonymousMemory(nullptr, length, mmapAddr);
        TP_TRACE_END(TP_UBSM_SHM_MMAP_ADDR, ret);
        if (BresultFail(ret)) {
            DBG_LOGERROR("MemoryMap2MAligned failed, length is " << length);
            return MXM_ERR_MMAP_VA_FAILED;
        }
        result = mmapAddr;
        return MXM_OK;
    }
    if (MapWithFixedFlags(flags)) {
        result = start;
        return MXM_OK;
    }
    // 指定地址，但是不带MAP_FIXED，先尝试能否匿名map这段地址，无论返回什么地址，都用返回的地址
    auto ret = RackMemFdMap::MapAnonymousMemory(start, length, mmapAddr);
    if (BresultFail(ret)) {
        DBG_LOGERROR("MapAnonymousMemory failed, length is " << length);
        return ret;
    }
    result = mmapAddr;
    return MXM_OK;
}

/**
 * 1.参数检查，2.重复map检查，3。从agent获取memid。4.obmm_memid_to_fd.5.poxsi map 6.update meta data
 */
int RackMemShm::UbsMemShmMmap(void *start, size_t mapSize, int prot, int flags, const std::string &name, off_t off,
                              void **local_ptr)
{
    if (local_ptr == nullptr) {
        DBG_LOGERROR("address is empty.");
        return MXM_ERR_PARAM_INVALID;
    }

    auto ownStatus = GetOwnStatus(prot);
    if (ownStatus >= OWNBUFF) {
        DBG_LOGERROR("Invalid param, prot=" << prot);
        return MXM_ERR_PARAM_INVALID;
    }

    uint32_t ret = RackMemShmMmapParameterCheck(start, mapSize, prot, flags, name, off);
    if (BresultFail(ret)) {
        DBG_LOGERROR("Param invalid, name=" << name);
        return MXM_ERR_PARAM_INVALID;
    }
    std::vector<uint64_t> memIds{};
    size_t returnShmSize{0};
    size_t unitSize{0};
    uint64_t createFlags{0};
    int oflag{0};

    mode_t mode = convertToFileMode(ownStatus);
    DBG_LOGINFO("Shm mmap name=" << name << ", prot=" << prot << ", mode=" << mode << ", own status=" << ownStatus);
    TP_TRACE_BEGIN(TP_UBSM_SHM_MAP_IPC_REQUEST);
    ret = ShmIpcCommand::IpcCallShmMap(name, mapSize, memIds, returnShmSize, unitSize, createFlags, oflag, mode, prot);
    TP_TRACE_END(TP_UBSM_SHM_MAP_IPC_REQUEST, ret);
    if (BresultFail(ret)) {
        DBG_LOGERROR("Failed to mmap, name=" << name << ", mapSize=" << mapSize << ", ret=" << ret);
        return ret;
    }

    std::vector<std::string> mappingNames;
    ret = RegisterMappingNames(name, 1, mappingNames);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Recording attached shared memory name failed, name=" << name << ", ret=" << ret);
        const auto detachRet = ShmIpcCommand::IpcCallShmUnMap(name);
        if (detachRet != MXM_OK) {
            DBG_LOGERROR("Rolling back unrecorded shared memory attach failed, name=" << name << ", ret=" << detachRet);
        }
        return ret;
    }
    constexpr uint64_t supportedFlags = UBSM_FLAG_WITH_LOCK | UBSM_FLAG_NONCACHE | UBSM_FLAG_WR_DELAY_COMP |
                                        UBSM_FLAG_ONLY_IMPORT_NONCACHE | UBSM_FLAG_MEM_ANONYMOUS |
                                        UBSM_FLAG_MMAP_HUGETLB_PMD;
    if ((createFlags & ~supportedFlags) != 0) {
        ShmIpcCommand::IpcCallShmUnMap(name);
        ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
        DBG_LOGERROR("Unsupported shared-memory creation flags, name=" << name << ", createFlags=" << createFlags
                                                                       << ", supportedFlags=" << supportedFlags);
        return MXM_ERR_UBSE_NOT_SUPPORTED;
    }

    if (returnShmSize == 0 || mapSize != returnShmSize) {
        DBG_LOGERROR("Size error, mapSize=" << mapSize << ", ShmSize=" << returnShmSize);
        ShmIpcCommand::IpcCallShmUnMap(name);
        ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
        return MXM_ERR_PARAM_INVALID;
    }
    if (unitSize == 0) {
        DBG_LOGERROR("Unit size is zero, name=" << name);
        ShmIpcCommand::IpcCallShmUnMap(name);
        ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
        return MXM_ERR_CHECK_RESOURCE;
    }
    static uint64_t alignment = GetHugeTlbPmdSize();
    if ((createFlags & UBSM_FLAG_MMAP_HUGETLB_PMD) == UBSM_FLAG_MMAP_HUGETLB_PMD &&
        (alignment == 0 || mapSize % alignment != 0)) {
        DBG_LOGERROR("The mapSize is not aligned to " << alignment << ", mapSize=" << mapSize);
        ShmIpcCommand::IpcCallShmUnMap(name);
        ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
        return MXM_ERR_PARAM_INVALID;
    }

    void *mmapAddr = nullptr;
    uint64_t mmapCount = mapSize % unitSize == 0 ? mapSize / unitSize : mapSize / unitSize + 1;

    if ((createFlags & UBSM_FLAG_MMAP_HUGETLB_PMD) == UBSM_FLAG_MMAP_HUGETLB_PMD) {
        ret = GetPreAllocateAddressAligned(start, mapSize, flags, mmapAddr, alignment);
    } else {
        ret = GetPreAllocateAddress(start, mapSize, flags, mmapAddr);
    }
    if (BresultFail(ret)) {
        DBG_LOGERROR("GetPreAllocateAddress failed, name=" << name);
        ShmIpcCommand::IpcCallShmUnMap(name);
        ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
        return ret;
    }
    if (!MapWithFixedFlags(flags)) {
        flags |= MAP_FIXED;
    }

    DBG_LOGINFO("name=" << name << ", open flag=" << oflag << ", open_flag=" << createFlags << ", prot=" << prot);
    ShmAppMetaData shmAppMetaData{name, memIds, mmapAddr, returnShmSize, ownStatus, createFlags};
    shmAppMetaData.SetOflag(oflag);
    shmAppMetaData.SetUnitSize(unitSize);
    TP_TRACE_BEGIN(TP_UBSM_SHM_BIND_ADDR);
    ret = RackMemShmMmapByMemID(mmapAddr, unitSize, mmapCount, memIds, shmAppMetaData, createFlags, oflag, prot, flags);
    TP_TRACE_END(TP_UBSM_SHM_BIND_ADDR, ret);
    if (BresultFail(ret)) {
        DBG_LOGERROR("RackMemShmMmapByMemID failed, name=" << name);
        UnMapWhenError(mmapAddr, mapSize, name, shmAppMetaData);
        ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
        return ret;
    }

    auto addResult = ShmMetaDataMgr::GetInstance().AddMetaData(name, mmapAddr, shmAppMetaData);
    if (BresultFail(addResult)) {
        DBG_LOGERROR("AddMetaData error " << addResult);
        UnMapWhenError(mmapAddr, mapSize, name, shmAppMetaData);
        ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
        return addResult;
    }
    ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
    *local_ptr = mmapAddr;
    return UBSM_OK;
}

static uint32_t ValidateMultiNodeMapResponse(const MultiNodeShmMapResponse &response, size_t mapSize)
{
    if (response.memIds_.size() < 2 || response.memIds_.size() > MAX_MULTINODE_HOST_NUM ||
        response.memIds_.size() != response.groupSizes_.size() ||
        response.memIds_.size() != response.groupFlags_.size() || response.unitSize_ == 0 ||
        response.unitSize_ > std::numeric_limits<size_t>::max() || response.createdSize_ != mapSize ||
        (response.oflag_ != O_RDONLY && response.oflag_ != O_RDWR)) {
        DBG_LOGERROR("Invalid multi-node mapping response, groupCount="
                     << response.memIds_.size() << ", sizeCount=" << response.groupSizes_.size()
                     << ", flagCount=" << response.groupFlags_.size() << ", unitSize=" << response.unitSize_
                     << ", createdSize=" << response.createdSize_ << ", mapSize=" << mapSize
                     << ", oflag=" << response.oflag_);
        return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
    }
    uint64_t total{};
    for (size_t index = 0; index < response.groupSizes_.size(); ++index) {
        const auto size = response.groupSizes_[index];
        const auto count = size / response.unitSize_ + (size % response.unitSize_ != 0);
        if ((response.groupFlags_[index] & ~UBSM_FLAG_ONLY_IMPORT_NONCACHE) !=
                (response.flag_ & ~UBSM_FLAG_ONLY_IMPORT_NONCACHE) ||
            size == 0 || size > mapSize - total || size % RACK_MEM_PAGE_SIZE != 0 ||
            count != response.memIds_[index].size()) {
            DBG_LOGERROR("Invalid multi-node mapping group, index="
                         << index << ", size=" << size << ", memIdCount=" << response.memIds_[index].size()
                         << ", flag=" << response.groupFlags_[index] << ", poolFlag=" << response.flag_
                         << ", expectedCount=" << count);
            return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
        }
        total += size;
    }
    if (total != mapSize) {
        DBG_LOGERROR("Multi-node mapping group size sum mismatch, sum=" << total << ", mapSize=" << mapSize);
        return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
    }
    return MXM_OK;
}

static uint32_t MapMultiNodeGroups(ShmAppMetaData &metadata, int prot, int flags)
{
    metadata.slices.reserve(metadata.slices.size() + metadata.memIds.size());
    metadata.addrs.reserve(metadata.addrs.size() + metadata.memIds.size());
    size_t offset{};
    for (size_t index = 0; index < metadata.groupMemIds.size(); ++index) {
        auto address = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(metadata.addr) + offset);
        ShmAppMetaData group{
            metadata.name,       metadata.groupMemIds[index], address, static_cast<size_t>(metadata.groupSizes[index]),
            metadata.ownStatuss, metadata.groupFlags[index]};
        DBG_LOGINFO("Mapping multi-node shared memory group, name="
                    << metadata.name << ", index=" << index << ", offset=" << offset << ", size=" << group.mapSize
                    << ", memIdCount=" << group.memIds.size());
        const auto ret = RackMemShmMmapByMemID(address, metadata.unitSize, group.memIds.size(), group.memIds, group,
                                               group.flag, metadata.oflag, prot, flags);
        metadata.slices.insert(metadata.slices.end(), group.slices.begin(), group.slices.end());
        metadata.addrs.insert(metadata.addrs.end(), group.addrs.begin(), group.addrs.end());
        if (ret != MXM_OK) {
            DBG_LOGERROR("Mapping multi-node shared memory group failed, name=" << metadata.name << ", index=" << index
                                                                                << ", ret=" << ret);
            return ret;
        }
        offset += group.mapSize;
    }
    return MXM_OK;
}

static int ValidateInterleaveSize(const MultiNodeShmMapResponse &response, size_t interleaveSize)
{
    if (interleaveSize == 0) {
        return MXM_OK;
    }
    const auto pageSize = sysconf(_SC_PAGESIZE);
    const auto alignment = (response.flag_ & UBSM_FLAG_MMAP_HUGETLB_PMD) != 0 ? GetHugeTlbPmdSize() : pageSize;
    if (pageSize <= 0 || alignment == 0 || interleaveSize % alignment != 0 || response.unitSize_ % alignment != 0) {
        DBG_LOGERROR("Interleaved mapping is not aligned, interleaveSize="
                     << interleaveSize << ", unitSize=" << response.unitSize_ << ", alignment=" << alignment);
        return MXM_ERR_PARAM_INVALID;
    }
    for (size_t index = 0; index < response.groupSizes_.size(); ++index) {
        if (response.groupSizes_[index] % interleaveSize != 0) {
            DBG_LOGERROR("Interleave size does not divide provider size, index="
                         << index << ", groupSize=" << response.groupSizes_[index]
                         << ", interleaveSize=" << interleaveSize);
            return MXM_ERR_PARAM_INVALID;
        }
    }
    return MXM_OK;
}

// At most one current device per provider; release it after its final segment.
struct TemporaryMultiNodeDevices {
    std::vector<int> fds;
    explicit TemporaryMultiNodeDevices(size_t count) : fds(count, -1) {}
    void Close(size_t index)
    {
        if (fds[index] >= 0) {
            SystemAdapter::Close(fds[index]);
            fds[index] = -1;
            temporaryObmmFds.fetch_sub(1);
        }
    }
    ~TemporaryMultiNodeDevices()
    {
        for (size_t index = 0; index < fds.size(); ++index) {
            Close(index);
        }
    }
    TemporaryMultiNodeDevices(const TemporaryMultiNodeDevices &) = delete;
    TemporaryMultiNodeDevices &operator=(const TemporaryMultiNodeDevices &) = delete;
};

static int MapInterleaveChunk(ShmAppMetaData &metadata, TemporaryMultiNodeDevices &devices, size_t providerOffset,
                              size_t targetOffset, size_t group, int prot, int flags)
{
    size_t remaining = metadata.interleaveSize;
    while (remaining != 0) {
        const auto fdIndex = providerOffset / metadata.unitSize;
        const auto offset = providerOffset % metadata.unitSize;
        const auto size = std::min(remaining, metadata.unitSize - offset);
        if (fdIndex >= metadata.groupMemIds[group].size() ||
            offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
            DBG_LOGERROR("Invalid interleaved mapping fd offset, name=" << metadata.name << ", fdIndex=" << fdIndex
                                                                        << ", offset=" << offset);
            return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
        }
        if (devices.fds[group] < 0) {
            const auto id = metadata.groupMemIds[group][fdIndex];
            const auto fd = ObmmOpenInternal(id, metadata.groupFlags[group], metadata.oflag);
            if (fd < 0) {
                DBG_LOGERROR("Opening interleaved device failed, name=" << metadata.name << ", group=" << group
                                                                        << ", memId=" << id << ", fd=" << fd);
                return MXM_ERR_SHM_OBMM_OPEN;
            }
            devices.fds[group] = fd;
            temporaryObmmFds.fetch_add(1);
        }
        off_t mapOffset = static_cast<off_t>(offset);
        if ((metadata.flag & UBSM_FLAG_MMAP_HUGETLB_PMD) != 0) {
            mapOffset |= OBMM_MMAP_FLAG_HUGETLB_PMD;
        }
        auto address = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(metadata.addr) + targetOffset);
        const auto ret =
            RackMemFdMap::GetInstance().MapForEachFd(address, size, devices.fds[group], prot, flags, mapOffset);
        if (ret != MXM_OK) {
            DBG_LOGERROR("Mapping interleaved segment failed, name="
                         << metadata.name << ", fdIndex=" << fdIndex << ", providerOffset=" << providerOffset
                         << ", targetOffset=" << targetOffset << ", size=" << size << ", ret=" << ret);
            return ret;
        }
        if (offset + size == metadata.unitSize || providerOffset + size == metadata.groupSizes[group]) {
            devices.Close(group);
        }
        providerOffset += size;
        targetOffset += size;
        remaining -= size;
    }
    return MXM_OK;
}

static int MapMultiNodeInterleaved(ShmAppMetaData &metadata, int prot, int flags)
{
    DBG_LOGINFO("Mapping multi-node shared memory with interleaving, name="
                << metadata.name << ", providerCount=" << metadata.groupSizes.size() << ", mapSize=" << metadata.mapSize
                << ", interleaveSize=" << metadata.interleaveSize);
    TemporaryMultiNodeDevices devices(metadata.groupSizes.size());
    std::vector<size_t> providerOffsets(metadata.groupSizes.size(), 0);
    size_t targetOffset{};
    while (targetOffset < metadata.mapSize) {
        for (size_t index = 0; index < metadata.groupSizes.size(); ++index) {
            if (providerOffsets[index] < metadata.groupSizes[index]) {
                const auto ret =
                    MapInterleaveChunk(metadata, devices, providerOffsets[index], targetOffset, index, prot, flags);
                if (ret != MXM_OK) {
                    DBG_LOGERROR("Mapping interleaved provider chunk failed, name="
                                 << metadata.name << ", providerIndex=" << index << ", ret=" << ret);
                    return ret;
                }
                providerOffsets[index] += metadata.interleaveSize;
                targetOffset += metadata.interleaveSize;
            }
        }
    }
    return MXM_OK;
}

static void RollbackMultiNodeMap(void *address, size_t size, const std::string &name, ShmAppMetaData &metadata)
{
    if (address != nullptr && SystemAdapter::MemoryUnMap(address, size) == -1) {
        DBG_LOGERROR("Rolling back multi-node mapping address failed, name=" << name << ", size=" << size
                                                                             << ", errno=" << errno);
    }
    const auto ret = ShmIpcCommand::IpcCallShmUnmapMultiNodePool(name);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Rolling back multi-node pool attach failed, name=" << name << ", ret=" << ret);
    }
}

static int MapAndSaveMultiNodePool(void *address, size_t mapSize, int prot, int flags, const std::string &name,
                                   const MultiNodeShmMapResponse &response, size_t interleaveSize = 0)
{
    ShmAppMetaData metadata{name, {}, address, mapSize, GetOwnStatus(prot), response.flag_};
    metadata.SetUnitSize(response.unitSize_);
    metadata.SetOflag(response.oflag_);
    metadata.groupMemIds = response.memIds_;
    metadata.groupSizes = response.groupSizes_;
    metadata.groupFlags = response.groupFlags_;
    metadata.interleaveSize = interleaveSize;
    for (const auto &group : response.memIds_) {
        metadata.memIds.insert(metadata.memIds.end(), group.begin(), group.end());
    }
    auto ret = interleaveSize == 0 ? MapMultiNodeGroups(metadata, prot, flags) :
                                     MapMultiNodeInterleaved(metadata, prot, flags);
    if (ret == MXM_OK) {
        ret = ShmMetaDataMgr::GetInstance().AddMetaData(name, address, metadata);
        if (ret != MXM_OK) {
            DBG_LOGERROR("Saving multi-node mapping metadata failed, name=" << name << ", ret=" << ret);
        }
    }
    if (ret != MXM_OK) {
        RollbackMultiNodeMap(address, mapSize, name, metadata);
    }
    return ret;
}

static int PrepareMultiNodeMappingAddress(RackMemShm &shm, void *start, size_t mapSize, int flags,
                                          const MultiNodeShmMapResponse &response, void *&address)
{
    const bool hugePages = (response.flag_ & UBSM_FLAG_MMAP_HUGETLB_PMD) != 0;
    const auto alignment = hugePages ? GetHugeTlbPmdSize() : 1;
    if (hugePages) {
        for (const auto size : response.groupSizes_) {
            if (alignment == 0 || size % alignment != 0 || response.unitSize_ % alignment != 0) {
                DBG_LOGERROR("Multi-node group is not huge-page aligned, size="
                             << size << ", unitSize=" << response.unitSize_ << ", alignment=" << alignment);
                return MXM_ERR_PARAM_INVALID;
            }
        }
    }
    if (MapWithFixedFlags(flags)) {
        if (reinterpret_cast<uintptr_t>(start) % alignment != 0) {
            DBG_LOGERROR("Multi-node fixed mapping address is not aligned, alignment=" << alignment);
            return MXM_ERR_PARAM_INVALID;
        }
        const int reserveFlags = MAP_PRIVATE | MAP_ANONYMOUS | (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE));
        auto reserved = SystemAdapter::MemoryMap(start, mapSize, PROT_NONE, reserveFlags, -1, 0);
        if (reserved == MAP_FAILED) {
            DBG_LOGERROR("Reserving multi-node fixed mapping address failed, size=" << mapSize << ", errno=" << errno);
            return MXM_ERR_MMAP_VA_FAILED;
        }
        address = reserved;
        if (reserved != start) {
            DBG_LOGERROR("Multi-node fixed mapping reservation returned an unexpected address");
            return MXM_ERR_MMAP_VA_FAILED;
        }
        return MXM_OK;
    }
    return hugePages ? shm.GetPreAllocateAddressAligned(start, mapSize, flags, address, alignment) :
                       shm.GetPreAllocateAddress(start, mapSize, flags, address);
}

int RackMemShm::UbsMemShmMapMultiNodePool(void *start, size_t mapSize, int prot, int flags, const std::string &name,
                                          off_t off, void **localPtr, size_t interleaveSize)
{
    std::lock_guard<std::mutex> transactionLock(multiNodeMapTransactionMutex);
    if (localPtr == nullptr) {
        DBG_LOGERROR("Multi-node mapping output pointer is null, name=" << name);
        return MXM_ERR_PARAM_INVALID;
    }
    auto ret = RackMemShmMmapParameterCheck(start, mapSize, prot, flags, name, off);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Invalid multi-node mapping parameters, name=" << name << ", ret=" << ret);
        return ret;
    }
    MultiNodeShmMapResponse response;
    ret = ShmIpcCommand::IpcCallShmMapMultiNodePool(name, mapSize, prot, response);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Preparing multi-node pool attach transaction failed, name=" << name << ", ret=" << ret);
        return ret;
    }
    std::vector<std::string> mappingNames;
    ret = RegisterMappingNames(name, response.memIds_.size(), mappingNames);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Recording attached multi-node members failed, name=" << name << ", ret=" << ret);
        ShmAppMetaData empty;
        RollbackMultiNodeMap(nullptr, 0, name, empty);
        return ret;
    }
    void *address = nullptr;
    bool mappingStarted = false;
    do {
        ret = ValidateMultiNodeMapResponse(response, mapSize);
        if (ret != MXM_OK) {
            break;
        }
        ret = ValidateInterleaveSize(response, interleaveSize);
        if (ret != MXM_OK) {
            break;
        }
        ret = PrepareMultiNodeMappingAddress(*this, start, mapSize, flags, response, address);
        if (ret != MXM_OK) {
            break;
        }
        // Once mapping starts, MapAndSaveMultiNodePool owns failure rollback.
        mappingStarted = true;
        ret = MapAndSaveMultiNodePool(address, mapSize, prot, (flags & ~MAP_FIXED_NOREPLACE) | MAP_FIXED, name,
                                      response, interleaveSize);
    } while (false);
    if (ret == MXM_OK) {
        *localPtr = address;
    } else if (!mappingStarted) {
        DBG_LOGERROR("Preparing multi-node mapping failed, name=" << name << ", ret=" << ret);
        ShmAppMetaData empty;
        RollbackMultiNodeMap(address, mapSize, name, empty);
    }
    ShmMetaDataMgr::GetInstance().RemoveMappingInProgressNames(mappingNames);
    return ret;
}

static uint32_t RevokeLocalShmMapping(void *start, ShmAppMetaData &metadata)
{
    if (!metadata.IsMappingRevoked()) {
        auto address = SystemAdapter::MemoryMap(start, metadata.mapSize, PROT_NONE,
                                                MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0L);
        if (address == MAP_FAILED) {
            DBG_LOGERROR("Revoking shared memory mapping failed, name=" << metadata.name << ", errno=" << errno);
            return MXM_ERR_MMAP_VA_FAILED;
        }
        if (!DontForkMapping(address, metadata.mapSize)) {
            return MXM_ERR_MMAP_VA_FAILED;
        }
        metadata.SetMappingRevoked(true);
    }
    const auto ret = ShmMetaDataMgr::GetInstance().UpdateMetaData(metadata.name, metadata);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Saving revoked shared memory mapping state failed, name=" << metadata.name << ", ret=" << ret);
    }
    return ret;
}

static uint32_t ReleaseShmServerReferences(ShmAppMetaData &metadata)
{
    if (!metadata.detachCompleted) {
        const auto ret = metadata.groupMemIds.empty() ? ShmIpcCommand::IpcCallShmUnMap(metadata.name) :
                                                        ShmIpcCommand::IpcCallShmUnmapMultiNodePool(metadata.name);
        if (ret != MXM_OK) {
            DBG_LOGERROR("Releasing shared memory server references failed; retry unmap, name=" << metadata.name
                                                                                                << ", ret=" << ret);
            return ret;
        }
        metadata.detachCompleted = true;
    }
    const auto ret = ShmMetaDataMgr::GetInstance().UpdateMetaData(metadata.name, metadata);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Saving shared memory detach state failed, name=" << metadata.name << ", ret=" << ret);
    }
    return ret;
}

static uint32_t ReleaseShmMappingAddress(void *start, ShmAppMetaData &metadata)
{
    if (!metadata.addressReleased) {
        if (SystemAdapter::MemoryUnMap(start, metadata.mapSize) == -1) {
            DBG_LOGERROR("Releasing shared memory address failed; retry unmap, name="
                         << metadata.name << ", size=" << metadata.mapSize << ", errno=" << errno);
            return MXM_ERR_MMAP_VA_FAILED;
        }
        metadata.addressReleased = true;
    }
    const auto ret = ShmMetaDataMgr::GetInstance().UpdateMetaData(metadata.name, metadata);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Saving released shared memory address state failed, name=" << metadata.name << ", ret=" << ret);
    }
    return ret;
}

uint32_t RackMemShm::UbsMemShmUnmmap(void *start, size_t size)
{
    if (start == nullptr) {
        DBG_LOGERROR("Shared memory unmap address is null");
        return MXM_ERR_PARAM_INVALID;
    }
    std::string name;
    auto ret = ShmMetaDataMgr::GetInstance().GetNameByAddr(start, name);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("GetNameByAddr error, ret=" << ret);
        return ret;
    }

    auto metaLockGuard = ShmMetaDataMgr::GetInstance().LockMeta(name);
    // The first lookup only selects the operation lock. Revalidate after waiting for it.
    ShmAppMetaData metadata;
    ret = ShmMetaDataMgr::GetInstance().GetShmMetaFromName(name, metadata);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("GetShmMetaFromName failed during unmap, name=" << name << ", ret=" << ret);
        return ret;
    }
    if (metadata.addr != start) {
        DBG_LOGERROR("Mapping changed while waiting for unmap lock, name=" << name);
        return MXM_ERR_SHM_NOT_FOUND;
    }
    if (metadata.mapSize != size) {
        DBG_LOGERROR("Map size not match, name=" << name << ", size=" << size << ", mapSize=" << metadata.mapSize);
        return MXM_ERR_PARAM_INVALID;
    }
    if (metadata.isLocked) {
        DBG_LOGERROR("Shared memory mapping is locked, name=" << metadata.name);
        return MXM_ERR_LOCK_ALREADY_LOCKED;
    }
    ret = RevokeLocalShmMapping(start, metadata);
    if (ret != MXM_OK) {
        return ret;
    }
    ret = ReleaseShmServerReferences(metadata);
    if (ret != MXM_OK) {
        return ret;
    }
    ret = ReleaseShmMappingAddress(start, metadata);
    if (ret != MXM_OK) {
        return ret;
    }
    ret = ShmMetaDataMgr::GetInstance().RemoveMetaData(start, metadata.name);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Removing released shared memory metadata failed; retry unmap, name=" << metadata.name
                                                                                           << ", ret=" << ret);
    }
    return ret;
}

uint32_t RackMemShm::UbsMemShmDelete(const std::string &name)
{
    auto ret = ShmIpcCommand::IpcCallShmDelete(name);
    if (BresultFail(ret)) {
        DBG_LOGERROR("ShmIpcCommand::IpcCallShmDelete Delete error " << ret);
        return ret;
    }
    return UBSM_OK;
}

/*
 * baseNid为-1，则在agent端进行检验，不为-1，则分为all2all和one2all两种类型进行检验
 *                                 */
uint32_t RackMemShm::CheckRegionPar(const std::string &baseNid, const SHMRegionDesc &shmRegion)
{
    if (shmRegion.num > MEM_TOPOLOGY_MAX_HOSTS || shmRegion.num <= 0) {
        DBG_LOGERROR("Number(" << shmRegion.num << ") in Region is invalid.");
        return MXM_ERR_PARAM_INVALID;
    }
    if (shmRegion.type >= RackMemShmRegionType::INCLUDE_ALL_TYPE || shmRegion.type < 0) {
        DBG_LOGERROR("The shmRegions type is error, type=" << shmRegion.type);
        return MXM_ERR_PARAM_INVALID;
    }

    if (baseNid.empty()) {
        return UBSM_OK;
    }
    if (shmRegion.type == RackMemShmRegionType::ONE2ALL_SHARE) {
        if (shmRegion.nodeId[0] != baseNid) {
            DBG_LOGERROR("The region type is one2all, base node id is "
                         << baseNid << ", nodeId[0] is " << shmRegion.nodeId[0] << ", they should be the same.");
            return MXM_ERR_PARAM_INVALID;
        }
    }
    if (shmRegion.type == RackMemShmRegionType::ALL2ALL_SHARE) {
        for (int i = 0; i < shmRegion.num; i++) {
            if (baseNid == shmRegion.nodeId[i]) {
                return UBSM_OK;
            }
        }
        DBG_LOGERROR("The region type is all2all, but the node id " << baseNid << " is not in region.");
        return MXM_ERR_PARAM_INVALID;
    }
    return UBSM_OK;
}

static int ValidateInterleavedOwnershipLayout(const ShmAppMetaData &meta)
{
    if (meta.unitSize == 0 || meta.unitSize % FOUR_KB != 0 || meta.interleaveSize % FOUR_KB != 0 ||
        meta.groupSizes.size() < 2 || meta.groupSizes.size() > MAX_MULTINODE_HOST_NUM ||
        meta.groupSizes.size() != meta.groupMemIds.size() || meta.groupSizes.size() != meta.groupFlags.size() ||
        !meta.addrs.empty()) {
        DBG_LOGERROR("Invalid interleaved ownership layout, name=" << meta.name);
        return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
    }
    size_t total{};
    size_t fdCount{};
    for (size_t index = 0; index < meta.groupSizes.size(); ++index) {
        const auto size = meta.groupSizes[index];
        const auto count = size / meta.unitSize + (size % meta.unitSize != 0);
        if (size == 0 || size > meta.mapSize - total || size % meta.interleaveSize != 0 ||
            count != meta.groupMemIds[index].size() || count > meta.memIds.size() - fdCount) {
            DBG_LOGERROR("Invalid interleaved ownership group, name=" << meta.name << ", group=" << index
                                                                      << ", size=" << size << ", fdCount=" << count);
            return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
        }
        total += static_cast<size_t>(size);
        fdCount += static_cast<size_t>(count);
    }
    if (total != meta.mapSize || fdCount != meta.memIds.size()) {
        DBG_LOGERROR("Incomplete interleaved ownership metadata, name=" << meta.name << ", size=" << total
                                                                        << ", fdCount=" << fdCount);
        return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
    }
    return MXM_OK;
}

static int ValidateOwnershipMapping(const ShmAppMetaData &meta, void *start, size_t length)
{
    const auto begin = reinterpret_cast<uintptr_t>(start);
    const auto base = reinterpret_cast<uintptr_t>(meta.addr);
    if (start == nullptr || meta.addr == nullptr || length == 0 || begin < base || length > meta.mapSize ||
        begin - base > meta.mapSize - length || meta.mapSize > UINTPTR_MAX - base || (begin - base) % FOUR_KB != 0 ||
        length % FOUR_KB != 0) {
        DBG_LOGERROR("Invalid shared memory ownership range, name=" << meta.name << ", length=" << length);
        return MXM_ERR_PARAM_INVALID;
    }
    if (meta.IsMappingRevoked() || meta.addressReleased || meta.detachCompleted) {
        DBG_LOGERROR("Shared memory mapping is being released, name=" << meta.name);
        return MXM_ERR_SHM_NOT_FOUND;
    }
    if (meta.interleaveSize != 0) {
        return ValidateInterleavedOwnershipLayout(meta);
    }
    if (meta.addrs.empty() || meta.addrs.size() != meta.slices.size()) {
        DBG_LOGERROR("Invalid shared memory ownership segment layout, name=" << meta.name);
        return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
    }
    size_t covered{};
    for (size_t index = 0; index < meta.addrs.size(); ++index) {
        const auto &[address, size] = meta.addrs[index];
        if (reinterpret_cast<uintptr_t>(address) != base + covered || size == 0 || size > meta.mapSize - covered ||
            meta.slices[index].address != address || meta.slices[index].length != size) {
            DBG_LOGERROR("Invalid shared memory ownership segment, name=" << meta.name << ", index=" << index
                                                                          << ", size=" << size);
            return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
        }
        covered += size;
    }
    if (covered != meta.mapSize) {
        DBG_LOGERROR("Incomplete shared memory ownership layout, name=" << meta.name << ", covered=" << covered);
        return MXM_ERR_SHM_POOL_LAYOUT_CONFLICT;
    }
    return MXM_OK;
}

static int SwitchOwnershipRange(const ShmAppMetaData &meta, size_t fdIndex, uintptr_t from, uintptr_t to, int prot)
{
    mem_id id{};
    uint64_t openFlags = meta.flag;
    int accessFlags = static_cast<int>(meta.oflag);
    if (meta.interleaveSize == 0) {
        const auto &slice = meta.slices[fdIndex];
        id = slice.memId;
        openFlags = slice.openFlags;
        accessFlags = slice.accessFlags;
    } else {
        id = meta.memIds[fdIndex];
        size_t base{};
        for (size_t group = 0; group < meta.groupMemIds.size(); ++group) {
            if (fdIndex - base < meta.groupMemIds[group].size()) {
                openFlags = meta.groupFlags[group];
                break;
            }
            base += meta.groupMemIds[group].size();
        }
    }
    const int fd = ObmmOpenInternal(id, openFlags, accessFlags);
    if (fd < 0) {
        DBG_LOGERROR("Opening ownership device failed, name=" << meta.name << ", memId=" << id << ", openFlags="
                                                              << openFlags << ", accessFlags=" << accessFlags);
        return MXM_ERR_SHM_OBMM_OPEN;
    }
    TemporaryObmmFd fdGuard(fd);
    TP_TRACE_BEGIN(TP_USBM_OBMM_SET_OWERSHIP);
    const auto ret = RmLibObmmExecutor::GetInstance().obmmOwnershipSwitch(fd, reinterpret_cast<void *>(from),
                                                                          reinterpret_cast<void *>(to), prot);
    TP_TRACE_END(TP_USBM_OBMM_SET_OWERSHIP, ret);
    if (ret != 0) {
        DBG_LOGERROR("Switching shared memory segment ownership failed, name=" << meta.name << ", fdIndex=" << fdIndex
                                                                               << ", fd=" << fd << ", begin=" << from
                                                                               << ", end=" << to << ", ret=" << ret);
        return MXM_ERR_OBMM_INNER;
    }
    return MXM_OK;
}

static size_t InterleavedRoundOffset(const ShmAppMetaData &meta, size_t round)
{
    size_t offset{};
    for (const auto size : meta.groupSizes) {
        offset += static_cast<size_t>(std::min<uint64_t>(size, round * meta.interleaveSize));
    }
    return offset;
}

static size_t FindInterleavedRound(const ShmAppMetaData &meta, size_t offset)
{
    size_t low{};
    size_t high = *std::max_element(meta.groupSizes.begin(), meta.groupSizes.end()) / meta.interleaveSize;
    // Locate the requested range without walking all preceding chunks of a potentially very large pool.
    while (high - low > 1) {
        const auto middle = low + (high - low) / 2;
        if (InterleavedRoundOffset(meta, middle) <= offset) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

static int SwitchInterleavedChunkOwnership(const ShmAppMetaData &meta, size_t providerOffset, size_t fdBase,
                                           size_t target, size_t begin, size_t end, int prot)
{
    auto from = std::max(begin, target);
    const auto to = std::min(end, target + meta.interleaveSize);
    if (from >= to) {
        return MXM_OK;
    }
    providerOffset += from - target;
    const auto base = reinterpret_cast<uintptr_t>(meta.addr);
    while (from < to) {
        const auto fdIndex = fdBase + providerOffset / meta.unitSize;
        const auto size = std::min(to - from, meta.unitSize - providerOffset % meta.unitSize);
        const auto ret = SwitchOwnershipRange(meta, fdIndex, base + from, base + from + size, prot);
        if (ret != MXM_OK) {
            return ret;
        }
        from += size;
        providerOffset += size;
    }
    return MXM_OK;
}

static int SwitchInterleavedOwnership(const ShmAppMetaData &meta, void *start, size_t length, int prot)
{
    const auto begin = reinterpret_cast<uintptr_t>(start) - reinterpret_cast<uintptr_t>(meta.addr);
    const auto end = begin + length;
    auto round = FindInterleavedRound(meta, begin);
    auto target = InterleavedRoundOffset(meta, round);
    while (target < end) {
        const auto providerOffset = round * meta.interleaveSize;
        size_t fdBase{};
        for (size_t index = 0; index < meta.groupSizes.size() && target < end; ++index) {
            if (providerOffset < meta.groupSizes[index]) {
                const auto ret =
                    SwitchInterleavedChunkOwnership(meta, providerOffset, fdBase, target, begin, end, prot);
                if (ret != MXM_OK) {
                    DBG_LOGERROR("Switching interleaved ownership failed, name="
                                 << meta.name << ", group=" << index << ", round=" << round << ", ret=" << ret);
                    return ret;
                }
                target += meta.interleaveSize;
            }
            fdBase += meta.groupMemIds[index].size();
        }
        ++round;
    }
    return MXM_OK;
}

static int SwitchSegmentOwnership(const ShmAppMetaData &meta, void *start, size_t length, int prot)
{
    if (meta.interleaveSize != 0) {
        return SwitchInterleavedOwnership(meta, start, length, prot);
    }
    const auto begin = reinterpret_cast<uintptr_t>(start);
    const auto end = begin + length;
    for (size_t index = 0; index < meta.addrs.size(); ++index) {
        const auto segment = reinterpret_cast<uintptr_t>(meta.addrs[index].first);
        const auto from = std::max(begin, segment);
        const auto to = std::min(end, segment + meta.addrs[index].second);
        if (from >= to) {
            continue;
        }
        const auto ret = SwitchOwnershipRange(meta, index, from, to, prot);
        if (ret != MXM_OK) {
            return ret;
        }
    }
    return MXM_OK;
}

int32_t RackMemShm::UbsMemShmSetOwnerShipOpt(ShmAppMetaData &meta, void *start, size_t length, ShmOwnStatus status)
{
    const auto ret = ValidateOwnershipMapping(meta, start, length);
    if (ret != MXM_OK) {
        DBG_LOGERROR("Validating shared memory ownership mapping failed, name=" << meta.name << ", ret=" << ret);
        return ret;
    }
    int prot = PROT_NONE;
    switch (status) {
        case RackMemShmOwnStatus::UNACCESS:
            DBG_LOGDEBUG("Access state change to OBMM_ACCESS_NONE metaData.name=" << meta.name);
            prot = PROT_NONE;
            break;
        case RackMemShmOwnStatus::READ:
            DBG_LOGDEBUG("Access state change to OBMM_ACCESS_READONLY metaData.name=" << meta.name);
            prot = PROT_READ;
            break;
        case RackMemShmOwnStatus::READWRITE:
            DBG_LOGDEBUG("Access state change to OBMM_ACCESS_READWRITE metaData.name=" << meta.name);
            prot = PROT_READ | PROT_WRITE;
            break;
        case RackMemShmOwnStatus::WRITE:
            DBG_LOGDEBUG("Access state change to OBMM_ACCESS_WRITE metaData.name=" << meta.name);
            prot = PROT_WRITE;
            break;
        default:
            DBG_LOGERROR("Invalid shared memory ownership status, name=" << meta.name << ", status=" << status);
            return MXM_ERR_PARAM_INVALID;
    }
    constexpr uint64_t NONCACHE_OWNERSHIP_FLAGS = UBSM_FLAG_ONLY_IMPORT_NONCACHE | UBSM_FLAG_NONCACHE;
    if ((meta.flag & NONCACHE_OWNERSHIP_FLAGS) != 0) {
        DBG_LOGERROR("Ownership switching is not supported for noncache shared memory, name=" << meta.name << ", flags="
                                                                                              << meta.flag);
        return MXM_ERR_PARAM_INVALID;
    }
    return SwitchSegmentOwnership(meta, start, length, prot);
}

int32_t RackMemShm::UbsMemShmSetOwnerShip(const std::string &name, void *start, size_t length, ShmOwnStatus status)
{
    if (start == nullptr) {
        DBG_LOGERROR("Param start is nullptr.");
        return MXM_ERR_PARAM_INVALID;
    }
    if (name.empty()) {
        DBG_LOGERROR("Param name is empty.");
        return MXM_ERR_PARAM_INVALID;
    }
    if (length <= 0) {
        DBG_LOGERROR("Param length is invalid.");
        return MXM_ERR_PARAM_INVALID;
    }
    auto metaLockGuard = ShmMetaDataMgr::GetInstance().LockMeta(name);
    ShmAppMetaData meta;
    auto result = ShmMetaDataMgr::GetInstance().GetShmMetaFromName(name, meta);
    if (BresultFail(result)) {
        DBG_LOGERROR("Looking up shared memory ownership metadata failed, name=" << name << ", ret=" << result);
        return result;
    }
    auto ret = UbsMemShmSetOwnerShipOpt(meta, start, length, status);
    if (ret != 0) {
        DBG_LOGERROR("UbsMemShmSetOwnerShipOpt failed, ret: " << ret);
        return ret;
    }
    return UBSM_OK;
}

uint32_t RackMemShm::RackMemLookupResourceRegions(const std::string &baseNid, ShmRegionType type, SHMRegions &list)
{
    const uint32_t hresult = ShmIpcCommand::IpcCallLookupRegionList(baseNid, type, list);
    if (BresultFail(hresult)) {
        DBG_LOGERROR("Failed to calling IpcCallLookupRegionList, error=" << hresult);
        return hresult;
    }
    return UBSM_OK;
}

uint32_t RackMemShm::RackMemCreateResourceRegion(const std::string &regionName, const SHMRegionDesc &region)
{
    const uint32_t hresult = ShmIpcCommand::IpcCallCreateRegion(regionName, region);
    if (BresultFail(hresult)) {
        DBG_LOGERROR("Failed to calling IpcCallCreateRegion, error=" << hresult);
        return hresult;
    }
    return UBSM_OK;
}

uint32_t RackMemShm::RackMemLookupResourceRegion(const std::string &regionName, SHMRegionDesc &region)
{
    const uint32_t hresult = ShmIpcCommand::IpcCallLookupRegion(regionName, region);
    if (BresultFail(hresult)) {
        DBG_LOGERROR("Failed to calling IpcCallLookupRegion, error=" << hresult);
        return hresult;
    }
    return UBSM_OK;
}

uint32_t RackMemShm::RackMemDestroyResourceRegion(const std::string &regionName)
{
    const uint32_t hresult = ShmIpcCommand::IpcCallDestroyRegion(regionName);
    if (BresultFail(hresult)) {
        DBG_LOGERROR("Failed to calling IpcCallDestroyRegion error=" << hresult);
        return hresult;
    }
    return UBSM_OK;
}

int32_t RackMemShm::UbsMemShmWriteLock(const std::string &name)
{
    auto metaLockGuard = ShmMetaDataMgr::GetInstance().LockMeta(name);
    ShmAppMetaData meta;
    auto result = ShmMetaDataMgr::GetInstance().GetShmMetaFromName(name, meta);
    if (BresultFail(result)) {
        DBG_LOGERROR("Find shm by addr failed, ret: " << result);
        return result;
    }
    if (meta.addrs.size() != meta.slices.size()) {
        return MXM_ERR_SHM_NOT_FOUND;
    }
    if (meta.name != name) {
        DBG_LOGERROR("Found shm name(" << meta.name << ") is not equal to param name(" << name << ").");
        return MXM_ERR_SHM_NOT_FOUND;
    }

    if (meta.flag != UBSM_FLAG_WITH_LOCK) {
        DBG_LOGERROR("shm name(" << name << ") allocate flag(" << meta.flag << ") is not UBSM_FLAG_WITH_LOCK.");
        return MXM_ERR_LOCK_NOT_READY;
    }
    TP_TRACE_BEGIN(TP_UBSM_WRITE_LOCK_IPC_REQUEST);
    auto ret = ShmIpcCommand::IpcShmemWriteLock(name);
    TP_TRACE_END(TP_UBSM_WRITE_LOCK_IPC_REQUEST, ret);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("IpcShmemWriteLock failed, ret is:" << ret);
        return ret;
    }
    TP_TRACE_BEGIN(TP_UBSM_WRITE_LOCK_SET_OWERSHIP);
    ret = RackMemShm::GetInstance().UbsMemShmSetOwnerShipOpt(meta, meta.addr, meta.mapSize, READWRITE);
    TP_TRACE_END(TP_UBSM_WRITE_LOCK_SET_OWERSHIP, ret);
    if (ret != 0) {
        DBG_LOGERROR("UbsMemShmSetOwnerShipOpt failed, ret:" << ret);
        ShmIpcCommand::IpcShmemUnLock(name);
        return ret;
    }

    meta.isLocked++; // 对应name的shm 加锁
    meta.lockedStatus = READWRITE;

    result = ShmMetaDataMgr::GetInstance().UpdateMetaData(name, meta);
    if (BresultFail(result)) {
        DBG_LOGERROR("Update shm meta by name failed, ret: " << result);
        auto ownershipRet = UbsMemShmSetOwnerShipOpt(meta, meta.addr, meta.mapSize, UNACCESS);
        if (BresultFail(ownershipRet)) {
            DBG_LOGERROR("Rollback write ownership failed, ret: " << ownershipRet);
        }
        auto unlockRet = ShmIpcCommand::IpcShmemUnLock(name);
        if (BresultFail(unlockRet)) {
            DBG_LOGERROR("Rollback write lock failed, ret: " << unlockRet);
        }
        return result;
    }
    DBG_LOGINFO("Ipc Shm WriteLock success, name=" << name);

    return UBSM_OK;
}

int32_t RackMemShm::UbsMemShmReadLock(const std::string &name)
{
    auto metaLockGuard = ShmMetaDataMgr::GetInstance().LockMeta(name);
    ShmAppMetaData meta;
    auto result = ShmMetaDataMgr::GetInstance().GetShmMetaFromName(name, meta);
    if (BresultFail(result)) {
        DBG_LOGERROR("Find shm by name failed, ret: " << result);
        return result;
    }
    if (meta.addrs.size() != meta.slices.size()) {
        return MXM_ERR_SHM_NOT_FOUND;
    }
    if (meta.name != name) {
        DBG_LOGERROR("Found shm name(" << meta.name << ") is not equal to param name(" << name << ").");
        return MXM_ERR_SHM_NOT_FOUND;
    }

    if (meta.flag != UBSM_FLAG_WITH_LOCK) {
        DBG_LOGERROR("shm name(" << name << ") allocate flag(" << meta.flag << ") is not UBSM_FLAG_WITH_LOCK.");
        return MXM_ERR_LOCK_NOT_READY;
    }
    TP_TRACE_BEGIN(TP_UBSM_READ_LOCK_IPC_REQUEST);
    auto ret = ShmIpcCommand::IpcShmemReadLock(name);
    TP_TRACE_END(TP_UBSM_READ_LOCK_IPC_REQUEST, ret);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("IpcShmemReadLock failed, ret is:" << ret);
        return ret;
    }
    TP_TRACE_BEGIN(TP_UBSM_READ_LOCK_SET_OWNERSHIP);
    ret = RackMemShm::GetInstance().UbsMemShmSetOwnerShipOpt(meta, meta.addr, meta.mapSize, READ);
    TP_TRACE_END(TP_UBSM_READ_LOCK_SET_OWNERSHIP, ret);
    if (ret != 0) {
        DBG_LOGERROR("UbsMemShmSetOwnerShipOpt failed, ret:" << ret);
        ShmIpcCommand::IpcShmemUnLock(name);
        return ret;
    }

    meta.isLocked++; // 对应name的shm 加锁
    meta.lockedStatus = READ;
    result = ShmMetaDataMgr::GetInstance().UpdateMetaData(name, meta);
    if (BresultFail(result)) {
        DBG_LOGERROR("Update shm meta by name failed, ret: " << result);
        auto ownershipRet = UbsMemShmSetOwnerShipOpt(meta, meta.addr, meta.mapSize, UNACCESS);
        if (BresultFail(ownershipRet)) {
            DBG_LOGERROR("Rollback read ownership failed, ret: " << ownershipRet);
        }
        auto unlockRet = ShmIpcCommand::IpcShmemUnLock(name);
        if (BresultFail(unlockRet)) {
            DBG_LOGERROR("Rollback read lock failed, ret: " << unlockRet);
        }
        return result;
    }
    DBG_LOGINFO("Ipc Shm ReadLock success, name=" << name);

    return UBSM_OK;
}

int32_t RackMemShm::UbsMemShmUnLock(const std::string &name)
{
    auto metaLockGuard = ShmMetaDataMgr::GetInstance().LockMeta(name);
    ShmAppMetaData meta;

    auto result = ShmMetaDataMgr::GetInstance().GetShmMetaFromName(name, meta);
    if (BresultFail(result)) {
        DBG_LOGERROR("Find shm by addr failed, ret: " << result);
        return result;
    }
    if (meta.addrs.size() != meta.slices.size()) {
        return MXM_ERR_SHM_NOT_FOUND;
    }
    if (meta.name != name) {
        DBG_LOGERROR("Found shm name(" << meta.name << ") is not equal to param name(" << name << ").");
        return MXM_ERR_SHM_NOT_FOUND;
    }

    if (meta.flag != UBSM_FLAG_WITH_LOCK) {
        DBG_LOGERROR("shm name(" << name << ") allocate flag(" << meta.flag << ") is not UBSM_FLAG_WITH_LOCK.");
        return MXM_ERR_LOCK_NOT_READY;
    }

    if (meta.isLocked <= 0) {
        DBG_LOGERROR("shm name(" << name << ") is not locked. ");
        return MXM_ERR_LOCK_NOT_FOUND;
    }
    const auto previousLockStatus = meta.lockedStatus;

    TP_TRACE_BEGIN(TP_UBSM_UNLOCK_SET_OWNERSHIP);
    auto ret = RackMemShm::GetInstance().UbsMemShmSetOwnerShipOpt(meta, meta.addr, meta.mapSize, UNACCESS);
    TP_TRACE_END(TP_UBSM_UNLOCK_SET_OWNERSHIP, ret);
    if (ret != 0) {
        DBG_LOGERROR("UbsMemShmSetOwnerShipOpt failed, ret:" << ret);
        return ret;
    }

    TP_TRACE_BEGIN(TP_UBSM_UNLOCK_IPC_REQUEST);
    ret = ShmIpcCommand::IpcShmemUnLock(name);
    TP_TRACE_END(TP_UBSM_UNLOCK_IPC_REQUEST, ret);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("IpcShmemUnLock failed, ret is:" << ret);
        auto ownershipRet = UbsMemShmSetOwnerShipOpt(meta, meta.addr, meta.mapSize, previousLockStatus);
        if (BresultFail(ownershipRet)) {
            DBG_LOGERROR("Rollback unlock ownership failed, ret: " << ownershipRet);
        }
        return ret;
    }
    meta.isLocked--; // 对应name的shm 解锁
    if (meta.isLocked == 0) {
        meta.lockedStatus = UNACCESS;
    }
    result = ShmMetaDataMgr::GetInstance().UpdateMetaData(name, meta);
    if (UNLIKELY(result != 0)) {
        DBG_LOGERROR("Update shm meta by name failed, ret: " << result);
        return result;
    }
    DBG_LOGINFO("Ipc Shm UnLock success, name=" << name);

    return UBSM_OK;
}

int32_t RackMemShm::UbsMemShmAttach(const std::string &name)
{
    auto ret = ShmIpcCommand::IpcCallShmAttach(name);
    if (BresultFail(ret)) {
        DBG_LOGERROR("attach failed ret :" << ret << ", name is " << name);
        return ret;
    }
    return UBSM_OK;
}

int32_t RackMemShm::UbsMemShmDetach(const std::string &name)
{
    auto ret = ShmIpcCommand::IpcCallShmDetach(name);
    if (BresultFail(ret)) {
        DBG_LOGERROR("detach failed ret :" << ret << ", name is " << name);
        return ret;
    }
    return UBSM_OK;
}

int32_t RackMemShm::UbsMemShmListLookup(const std::string &prefix, std::vector<ubsmem_shmem_desc_t> &shm_list)
{
    auto ret = ShmIpcCommand::IpcCallShmListLookup(prefix, shm_list);
    if (BresultFail(ret)) {
        DBG_LOGERROR("listlookup failed ret :" << ret << ", prefix is " << prefix);
        return ret;
    }
    return UBSM_OK;
}

int32_t RackMemShm::UbsMemShmLookup(const std::string &name, ubsmem_shmem_info_t &shm_info)
{
    auto ret = ShmIpcCommand::IpcCallShmLookup(name, shm_info);
    if (BresultFail(ret)) {
        DBG_LOGERROR("lookup failed ret :" << ret << ", name is " << name);
        return ret;
    }
    return UBSM_OK;
}

int RackMemShm::RpcQueryInfo(std::string &name)
{
    auto hresult = ShmIpcCommand::IpcCallRpcQuery(name);
    if (BresultFail(hresult)) {
        DBG_LOGERROR("RpcQueryInfo fail, res is " << hresult);
        return MXM_ERR_RPC_QUERY_ERROR;
    }
    return 0;
}

int32_t RackMemShm::UbsMemQueryNode(std::string &NodeId, bool &isNodeReady, bool isQueryMasterNode)
{
    auto hresult = ShmIpcCommand::IpcQueryNode(NodeId, isNodeReady, isQueryMasterNode);
    if (BresultFail(hresult)) {
        DBG_LOGERROR("UbsMemQueryNode fail, res is " << hresult);
        return MXM_ERR_MEMLIB;
    }
    return 0;
}

int32_t RackMemShm::UbsMemQueryDlockStatus(bool &isReady)
{
    auto hresult = ShmIpcCommand::IpcQueryDlockStatus(isReady);
    if (BresultFail(hresult)) {
        DBG_LOGERROR("UbsMemQueryDlockStatus fail, res is " << hresult);
        return MXM_ERR_MEMLIB;
    }
    return 0;
}
} // namespace ock::mxmd
// ock
