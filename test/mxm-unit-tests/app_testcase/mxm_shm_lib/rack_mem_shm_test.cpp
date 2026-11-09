/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <mockcpp/mockcpp.hpp>
#include <thread>

#include "RackMemShm.cpp"
#include "RackMemShm.h"
#include "ipc_proxy.h"
#include "shm_ipc_command.h"

#define MOCKER_CPP(api, TT) (MOCKCPP_NS::mockAPI((#api), (reinterpret_cast<TT>(api))))
namespace UT {
using namespace ock::mxmd;

class RackMemShmTest : public testing::Test {
protected:
    void SetUp() override
    {
        MOCKER_CPP(madvise, int (*)(void *, size_t, int)).stubs().will(returnValue(0));
        validAddr = malloc(4);
    }

    void TearDown() override
    {
        free(validAddr);
        validAddr = nullptr;
        mockcpp::GlobalMockObject::reset();
    }

    void *validAddr;
    size_t validSize = 4096;
    std::string validName = "test_shm";
    ShmAppMetaData shmMetaData;
    std::vector<uint64_t> memIds = {123, 456};
};

TEST_F(RackMemShmTest, MetaOperationLockSerializesSameName)
{
    auto &mgr = ShmMetaDataMgr::GetInstance();
    auto lock = mgr.LockMeta(validName);
    bool acquired = false;
    std::thread contender([&]() {
        acquired = lock.mutex()->try_lock();
        if (acquired) {
            lock.mutex()->unlock();
        }
    });
    contender.join();
    EXPECT_FALSE(acquired);
    lock.unlock();
    auto nextLock = mgr.LockMeta(validName);
    EXPECT_TRUE(nextLock.owns_lock());
}

TEST_F(RackMemShmTest, MetaOperationLocksAllowDifferentSegments)
{
    auto &mgr = ShmMetaDataMgr::GetInstance();
    std::string otherName;
    for (int i = 0; i < 1024; ++i) {
        otherName = "other_shm_" + std::to_string(i);
        if (mgr.StrToSegment(otherName) != mgr.StrToSegment(validName)) {
            break;
        }
    }
    ASSERT_NE(mgr.StrToSegment(otherName), mgr.StrToSegment(validName));
    auto lock = mgr.LockMeta(validName);
    auto otherLock = mgr.LockMeta(otherName);
    EXPECT_TRUE(otherLock.owns_lock());
    EXPECT_NE(lock.mutex(), otherLock.mutex());
}

TEST_F(RackMemShmTest, UnmapRevalidatesMetadataBeforeReleasingMapping)
{
    MOCKER_CPP(&ShmMetaDataMgr::GetNameByAddr, int32_t(*)(ShmMetaDataMgr *, void *, std::string &))
        .expects(once())
        .with(any(), eq(validAddr), outBound(validName))
        .will(returnValue(static_cast<int32_t>(UBSM_OK)));
    MOCKER_CPP(&ShmMetaDataMgr::GetShmMetaFromName, int32_t(*)(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &))
        .expects(once())
        .with(any(), eq(validName), any())
        .will(returnValue(static_cast<int32_t>(MXM_ERR_SHM_NOT_FOUND)));
    MOCKER(SystemAdapter::MemoryMap).expects(never());
    MOCKER(ShmIpcCommand::IpcCallShmUnMap).expects(never());

    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmUnmmap(validAddr, validSize), MXM_ERR_SHM_NOT_FOUND);
}

TEST_F(RackMemShmTest, LockMetadataFailureRevokesOwnershipAndReleasesServerLock)
{
    for (const auto status : {READ, READWRITE}) {
        ShmAppMetaData metadata;
        metadata.name = validName;
        metadata.addr = validAddr;
        metadata.mapSize = validSize;
        metadata.flag = UBSM_FLAG_WITH_LOCK;
        MOCKER_CPP(&ShmMetaDataMgr::GetShmMetaFromName,
                   int32_t(*)(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &))
            .stubs()
            .with(any(), any(), outBound(metadata))
            .will(returnValue(static_cast<int32_t>(MXM_OK)));
        MOCKER_CPP(&ShmMetaDataMgr::UpdateMetaData,
                   int (*)(ShmMetaDataMgr *, const std::string &, const ShmAppMetaData &))
            .expects(once())
            .will(returnValue(static_cast<int>(MXM_ERR_MALLOC_FAIL)));
        MOCKER_CPP(&RackMemShm::UbsMemShmSetOwnerShipOpt,
                   int32_t(*)(RackMemShm *, ShmAppMetaData &, void *, size_t, ShmOwnStatus))
            .expects(once())
            .with(any(), any(), eq(validAddr), eq(validSize), eq(status))
            .will(returnValue(static_cast<int32_t>(MXM_OK)));
        MOCKER_CPP(&RackMemShm::UbsMemShmSetOwnerShipOpt,
                   int32_t(*)(RackMemShm *, ShmAppMetaData &, void *, size_t, ShmOwnStatus))
            .expects(once())
            .with(any(), any(), eq(validAddr), eq(validSize), eq(UNACCESS))
            .will(returnValue(static_cast<int32_t>(MXM_OK)));
        MOCKER(ShmIpcCommand::IpcShmemReadLock).stubs().will(returnValue(static_cast<uint32_t>(MXM_OK)));
        MOCKER(ShmIpcCommand::IpcShmemWriteLock).stubs().will(returnValue(static_cast<uint32_t>(MXM_OK)));
        MOCKER(ShmIpcCommand::IpcShmemUnLock).expects(once()).will(returnValue(static_cast<uint32_t>(MXM_OK)));
        auto &shm = RackMemShm::GetInstance();
        EXPECT_EQ(status == READ ? shm.UbsMemShmReadLock(validName) : shm.UbsMemShmWriteLock(validName),
                  MXM_ERR_MALLOC_FAIL);
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}

TEST_F(RackMemShmTest, UnlockIpcFailureRestoresPreviousOwnership)
{
    ShmAppMetaData metadata;
    metadata.name = validName;
    metadata.addr = validAddr;
    metadata.mapSize = validSize;
    metadata.flag = UBSM_FLAG_WITH_LOCK;
    metadata.isLocked = 1;
    metadata.lockedStatus = READ;
    MOCKER_CPP(&ShmMetaDataMgr::GetShmMetaFromName, int32_t(*)(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &))
        .stubs()
        .with(any(), any(), outBound(metadata))
        .will(returnValue(static_cast<int32_t>(MXM_OK)));
    MOCKER_CPP(&ShmMetaDataMgr::UpdateMetaData, int (*)(ShmMetaDataMgr *, const std::string &, const ShmAppMetaData &))
        .expects(never());
    for (const auto status : {UNACCESS, READ}) {
        MOCKER_CPP(&RackMemShm::UbsMemShmSetOwnerShipOpt,
                   int32_t(*)(RackMemShm *, ShmAppMetaData &, void *, size_t, ShmOwnStatus))
            .expects(once())
            .with(any(), any(), eq(validAddr), eq(validSize), eq(status))
            .will(returnValue(static_cast<int32_t>(MXM_OK)));
    }
    MOCKER(ShmIpcCommand::IpcShmemUnLock)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_IPC_SYNC_CALL)));
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmUnLock(validName), MXM_ERR_IPC_SYNC_CALL);
    GlobalMockObject::verify();
}

static std::vector<uint64_t> multiNodeOpenedIds;
static std::vector<std::pair<uintptr_t, size_t>> multiNodeMappedRanges;

static int RecordMultiNodeOpen(mem_id id, int, int)
{
    multiNodeOpenedIds.emplace_back(id);
    return static_cast<int>(id + 100);
}

static uint32_t RecordMultiNodeMap(RackMemFdMap *instance, void *address, size_t size, int, int, int, off_t)
{
    multiNodeMappedRanges.emplace_back(reinterpret_cast<uintptr_t>(address), size);
    return MXM_OK;
}

static uint32_t FailSecondMultiNodeMap(RackMemFdMap *instance, void *address, size_t size, int fd, int prot, int flags,
                                       off_t offset)
{
    RecordMultiNodeMap(instance, address, size, fd, prot, flags, offset);
    return multiNodeMappedRanges.size() == 2 ? MXM_ERR_MMAP_VA_FAILED : MXM_OK;
}

static MultiNodeShmMapResponse MakeMultiNodeMapResponse()
{
    constexpr uint64_t mib = 1024UL * 1024;
    return MultiNodeShmMapResponse{MXM_OK, {{11}, {21, 22}}, 8 * mib, 16 * mib, 0, O_RDWR, {4 * mib, 12 * mib}};
}

static std::vector<std::pair<int, off_t>> interleavedFdOffsets;

static uint32_t RecordInterleavedMap(RackMemFdMap *instance, void *address, size_t size, int fd, int prot, int flags,
                                     off_t offset)
{
    interleavedFdOffsets.emplace_back(fd, offset);
    return RecordMultiNodeMap(instance, address, size, fd, prot, flags, offset);
}

static ShmAppMetaData MakeInterleavedMetadata(const MultiNodeShmMapResponse &response, size_t chunk)
{
    ShmAppMetaData metadata{"interleaved_pool",
                            {},
                            reinterpret_cast<void *>(0x10000000),
                            static_cast<size_t>(response.createdSize_),
                            READWRITE,
                            response.flag_};
    metadata.groupMemIds = response.memIds_;
    metadata.groupSizes = response.groupSizes_;
    metadata.groupFlags = response.groupFlags_;
    metadata.unitSize = response.unitSize_;
    metadata.oflag = response.oflag_;
    metadata.interleaveSize = chunk;
    for (const auto &group : response.memIds_) {
        metadata.memIds.insert(metadata.memIds.end(), group.begin(), group.end());
    }
    return metadata;
}

static void MockInterleavedMapping(size_t opens, size_t maps)
{
    multiNodeOpenedIds.clear();
    multiNodeMappedRanges.clear();
    interleavedFdOffsets.clear();
    MOCKER(ObmmOpenInternal).expects(exactly(opens)).will(invoke(RecordMultiNodeOpen));
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
        .expects(exactly(maps))
        .will(invoke(RecordInterleavedMap));
    MOCKER(SystemAdapter::Close).stubs().will(returnValue(0));
}

static std::vector<int> multiNodeOpenedFlags;

static int RecordMultiNodeOpenFlags(mem_id id, int flags, int oflag)
{
    multiNodeOpenedFlags.push_back(flags);
    return RecordMultiNodeOpen(id, flags, oflag);
}

TEST_F(RackMemShmTest, TestBothMultiNodeLayoutsOpenWithActualGroupFlags)
{
    for (const size_t chunk : {0UL, 4UL * 1024 * 1024}) {
        auto response = MakeMultiNodeMapResponse();
        response.groupFlags_ = {0, UBSM_FLAG_ONLY_IMPORT_NONCACHE};
        auto metadata = MakeInterleavedMetadata(response, chunk);
        multiNodeOpenedFlags.clear();
        MOCKER(ObmmOpenInternal).expects(exactly(3)).will(invoke(RecordMultiNodeOpenFlags));
        MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
            .stubs()
            .will(returnValue(static_cast<uint32_t>(MXM_OK)));
        MOCKER(SystemAdapter::Close).expects(exactly(3)).will(returnValue(0));
        EXPECT_EQ(ValidateMultiNodeMapResponse(response, response.createdSize_), MXM_OK);
        EXPECT_EQ(chunk == 0 ? MapMultiNodeGroups(metadata, PROT_READ, MAP_SHARED | MAP_FIXED) :
                               MapMultiNodeInterleaved(metadata, PROT_READ, MAP_SHARED | MAP_FIXED),
                  MXM_OK);
        EXPECT_EQ(multiNodeOpenedFlags,
                  (std::vector<int>{0, UBSM_FLAG_ONLY_IMPORT_NONCACHE, UBSM_FLAG_ONLY_IMPORT_NONCACHE}));
        EXPECT_EQ(temporaryObmmFds.load(), 0u);
        EXPECT_EQ(metadata.slices.size(), chunk == 0 ? 3u : 0u);
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}

TEST_F(RackMemShmTest, TestMultiNodeResponseRejectsMissingOrIncompatibleGroupFlags)
{
    auto response = MakeMultiNodeMapResponse();
    response.groupFlags_.pop_back();
    EXPECT_EQ(ValidateMultiNodeMapResponse(response, response.createdSize_), MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
    response.groupFlags_ = {0, UBSM_FLAG_MMAP_HUGETLB_PMD};
    EXPECT_EQ(ValidateMultiNodeMapResponse(response, response.createdSize_), MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
}

TEST_F(RackMemShmTest, TestHeartbeatReportsEveryMultiNodeMember)
{
    auto &manager = ShmMetaDataMgr::GetInstance();
    for (const size_t count : {2UL, 256UL}) {
        ShmAppMetaData metadata{"heartbeat_pool", {}, reinterpret_cast<void *>(0x50000000), 4096, READ, 0};
        metadata.groupMemIds.resize(count);
        ASSERT_EQ(manager.AddMetaData(metadata.name, metadata.addr, metadata), MXM_OK);
        std::vector<std::string> names;
        EXPECT_EQ(manager.GetAllUsedMemoryNames(names), MXM_OK);
        EXPECT_NE(std::find(names.begin(), names.end(), metadata.name), names.end());
        EXPECT_NE(std::find(names.begin(), names.end(), count == 2 ? "heartbeat_pool0101" : "heartbeat_poolffff"),
                  names.end());
        EXPECT_EQ(std::count_if(names.begin(), names.end(),
                                [](const std::string &name) { return name.find("heartbeat_pool") == 0; }),
                  count);
        EXPECT_EQ(manager.RemoveMetaData(metadata.addr, metadata.name), MXM_OK);
    }
}

TEST_F(RackMemShmTest, TestFailedMapRollbackDoesNotRetainDetachForRetry)
{
    ShmAppMetaData metadata;
    auto address = reinterpret_cast<void *>(0x50000000);
    MOCKER(SystemAdapter::Close).expects(never());
    MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_IPC_SYNC_CALL)));
    RollbackMultiNodeMap(address, 4096, "failed_pool", metadata);
    EXPECT_EQ(ShmMetaDataMgr::GetInstance().GetAddr("failed_pool"), nullptr);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestBothLayoutsAttemptRollbackOnce)
{
    for (const size_t chunk : {0UL, 4UL * 1024 * 1024}) {
        multiNodeMappedRanges.clear();
        MOCKER(ObmmOpenInternal).expects(exactly(2)).will(invoke(RecordMultiNodeOpen));
        MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
            .expects(exactly(2))
            .will(invoke(FailSecondMultiNodeMap));
        MOCKER(SystemAdapter::Close).expects(exactly(2)).will(returnValue(0));
        MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
        MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
            .expects(once())
            .will(returnValue(static_cast<uint32_t>(MXM_ERR_IPC_SYNC_CALL)));
        EXPECT_EQ(MapAndSaveMultiNodePool(reinterpret_cast<void *>(0x50000000), 16UL * 1024 * 1024, PROT_READ,
                                          MAP_SHARED | MAP_FIXED, "rollback_pool", MakeMultiNodeMapResponse(), chunk),
                  MXM_ERR_MMAP_VA_FAILED);
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}

TEST_F(RackMemShmTest, TestMappingInProgressNamesRetainAnotherMappingOfTheSameName)
{
    auto &manager = ShmMetaDataMgr::GetInstance();
    const std::vector<std::string> members{"same_pool", "same_pool0101"};
    ASSERT_EQ(manager.AddMappingInProgressNames(members), MXM_OK);
    ASSERT_EQ(manager.AddMappingInProgressNames(members), MXM_OK);
    manager.RemoveMappingInProgressNames(members);
    std::vector<std::string> names;
    ASSERT_EQ(manager.GetAllUsedMemoryNames(names), MXM_OK);
    for (const auto &member : members) {
        EXPECT_EQ(std::count(names.begin(), names.end(), member), 1);
    }
    manager.RemoveMappingInProgressNames(members);
    ASSERT_EQ(manager.GetAllUsedMemoryNames(names), MXM_OK);
    EXPECT_EQ(std::count(names.begin(), names.end(), members.front()), 0);
    EXPECT_EQ(std::count(names.begin(), names.end(), members.back()), 0);
}

TEST_F(RackMemShmTest, TestMappingInProgressNamesCoverOrdinaryAndMaximumPoolMembers)
{
    auto &manager = ShmMetaDataMgr::GetInstance();
    for (const size_t count : {1UL, 2UL, 256UL}) {
        std::vector<std::string> members;
        ASSERT_EQ(RegisterMappingNames("registered", count, members), MXM_OK);
        EXPECT_EQ(members.size(), count);
        EXPECT_EQ(members.front(), "registered");
        EXPECT_EQ(members.back(), count == 1 ? "registered" : (count == 2 ? "registered0101" : "registeredffff"));
        std::vector<std::string> names;
        ASSERT_EQ(manager.GetAllUsedMemoryNames(names), MXM_OK);
        for (const auto &member : members) {
            EXPECT_NE(std::find(names.begin(), names.end(), member), names.end());
        }
        manager.RemoveMappingInProgressNames(members);
        ASSERT_EQ(manager.GetAllUsedMemoryNames(names), MXM_OK);
        EXPECT_EQ(std::count(names.begin(), names.end(), "registered"), 0);
    }
}

TEST_F(RackMemShmTest, TestPublishedMetadataRemainsAfterMappingInProgressNamesAreRemoved)
{
    auto &manager = ShmMetaDataMgr::GetInstance();
    const std::vector<std::string> members{"published_pool", "published_pool0101"};
    ASSERT_EQ(manager.AddMappingInProgressNames(members), MXM_OK);
    ShmAppMetaData metadata{members.front(), {}, reinterpret_cast<void *>(0x50000000), FOUR_KB, READ, 0};
    metadata.groupMemIds.resize(2);
    ASSERT_EQ(manager.AddMetaData(metadata.name, metadata.addr, metadata), MXM_OK);
    manager.RemoveMappingInProgressNames(members);
    std::vector<std::string> names;
    ASSERT_EQ(manager.GetAllUsedMemoryNames(names), MXM_OK);
    for (const auto &member : members) {
        EXPECT_EQ(std::count(names.begin(), names.end(), member), 1);
    }
    EXPECT_EQ(manager.RemoveMetaData(metadata.addr, metadata.name), MXM_OK);
}

TEST_F(RackMemShmTest, TestMetadataRegistrationFailureReleasesLocks)
{
    auto &manager = ShmMetaDataMgr::GetInstance();
    ShmAppMetaData metadata{"metadata_oom", {}, reinterpret_cast<void *>(0x50000000), FOUR_KB, READ, 0};
    MOCKER_CPP(&ShmMetaDataMgr::AddMappedMemoryRange, int32_t(*)(ShmMetaDataMgr *, void *, size_t))
        .expects(once())
        .will(returnValue(static_cast<int32_t>(MXM_ERR_MALLOC_FAIL)));
    EXPECT_EQ(manager.AddMetaData(metadata.name, metadata.addr, metadata), MXM_ERR_MALLOC_FAIL);
    GlobalMockObject::verify();
    GlobalMockObject::reset();
    EXPECT_EQ(manager.GetAddr(metadata.name), nullptr);
    EXPECT_EQ(manager.AddMetaData(metadata.name, metadata.addr, metadata), MXM_OK);
    EXPECT_EQ(manager.RemoveMetaData(metadata.addr, metadata.name), MXM_OK);
}

TEST_F(RackMemShmTest, TestDuplicateMetadataDoesNotReplaceExistingMapping)
{
    auto &manager = ShmMetaDataMgr::GetInstance();
    ShmAppMetaData metadata{"metadata_duplicate", {}, reinterpret_cast<void *>(0x50000000), FOUR_KB, READ, 0};
    ASSERT_EQ(manager.AddMetaData(metadata.name, metadata.addr, metadata), MXM_OK);
    auto duplicate = metadata;
    duplicate.addr = reinterpret_cast<void *>(0x60000000);
    EXPECT_EQ(manager.AddMetaData(duplicate.name, duplicate.addr, duplicate), MXM_ERR_SHM_ALREADY_EXIST);
    EXPECT_EQ(manager.GetAddr(metadata.name), metadata.addr);
    EXPECT_FALSE(manager.HasOverlapWithKnownVmas(0x60000000, 0x60000000 + FOUR_KB));
    EXPECT_EQ(manager.RemoveMetaData(metadata.addr, metadata.name), MXM_OK);
}

TEST_F(RackMemShmTest, TestMetadataSaveFailureRollsBackOnce)
{
    MockInterleavedMapping(3, 3);
    MOCKER_CPP(&ShmMetaDataMgr::AddMetaData,
               uint32_t(*)(ShmMetaDataMgr *, const std::string &, void *, ShmAppMetaData &))
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_MALLOC_FAIL)));
    MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_IPC_SYNC_CALL)));
    EXPECT_EQ(MapAndSaveMultiNodePool(reinterpret_cast<void *>(0x50000000), 16UL * 1024 * 1024, PROT_READ,
                                      MAP_SHARED | MAP_FIXED, "oom_pool", MakeMultiNodeMapResponse()),
              MXM_ERR_MALLOC_FAIL);
    GlobalMockObject::verify();
}

static uint32_t ReturnPoolMapping(const std::string &, uint64_t, int, MultiNodeShmMapResponse &response)
{
    response = MakeMultiNodeMapResponse();
    return MXM_OK;
}

static std::promise<void> *publicationEntered{};
static std::shared_future<void> publicationRelease;

static uint32_t BlockMappingPublication(ShmMetaDataMgr *, const std::string &, void *, ShmAppMetaData &)
{
    publicationEntered->set_value();
    publicationRelease.wait();
    return MXM_OK;
}

TEST_F(RackMemShmTest, TestResourceSnapshotIncludesMappingBeforeMetadataPublication)
{
    std::promise<void> entered;
    std::promise<void> release;
    auto ready = entered.get_future();
    publicationEntered = &entered;
    publicationRelease = release.get_future().share();
    MockInterleavedMapping(3, 3);
    auto address = reinterpret_cast<void *>(0x50000000);
    MOCKER(SystemAdapter::MemoryMap).expects(once()).will(returnValue(address));
    MOCKER(ShmIpcCommand::IpcCallShmMapMultiNodePool).expects(once()).will(invoke(ReturnPoolMapping));
    MOCKER_CPP(&ShmMetaDataMgr::AddMetaData,
               uint32_t(*)(ShmMetaDataMgr *, const std::string &, void *, ShmAppMetaData &))
        .expects(once())
        .will(invoke(BlockMappingPublication));
    auto mapping = std::async(std::launch::async, [address] {
        void *result{};
        return RackMemShm::GetInstance().UbsMemShmMapMultiNodePool(address, 16UL * 1024 * 1024, PROT_READ,
                                                                   MAP_SHARED | MAP_FIXED, "in_progress", 0, &result);
    });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    std::vector<std::string> names;
    EXPECT_EQ(ShmMetaDataMgr::GetInstance().GetAllUsedMemoryNames(names), MXM_OK);
    EXPECT_NE(std::find(names.begin(), names.end(), "in_progress"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "in_progress0101"), names.end());
    release.set_value();
    EXPECT_EQ(mapping.get(), MXM_OK);
    EXPECT_EQ(ShmMetaDataMgr::GetInstance().GetAllUsedMemoryNames(names), MXM_OK);
    EXPECT_EQ(std::count(names.begin(), names.end(), "in_progress"), 0);
    EXPECT_EQ(std::count(names.begin(), names.end(), "in_progress0101"), 0);
    GlobalMockObject::verify();
}

static uint32_t FailDetachWithRegisteredNames(const std::string &name)
{
    std::vector<std::string> names;
    EXPECT_EQ(ShmMetaDataMgr::GetInstance().GetAllUsedMemoryNames(names), MXM_OK);
    EXPECT_NE(std::find(names.begin(), names.end(), name), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), name + "0101"), names.end());
    return MXM_ERR_IPC_SYNC_CALL;
}

TEST_F(RackMemShmTest, TestFailedMappingRemovesNamesAfterRollbackForBothLayouts)
{
    for (const size_t chunk : {0UL, 4UL * 1024 * 1024}) {
        multiNodeMappedRanges.clear();
        MOCKER(ObmmOpenInternal).expects(exactly(2)).will(invoke(RecordMultiNodeOpen));
        MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
            .expects(exactly(2))
            .will(invoke(FailSecondMultiNodeMap));
        MOCKER(SystemAdapter::Close).expects(exactly(2)).will(returnValue(0));
        MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
        auto address = reinterpret_cast<void *>(0x50000000);
        MOCKER(SystemAdapter::MemoryMap).expects(once()).will(returnValue(address));
        MOCKER(ShmIpcCommand::IpcCallShmMapMultiNodePool).expects(once()).will(invoke(ReturnPoolMapping));
        MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool).expects(once()).will(invoke(FailDetachWithRegisteredNames));
        void *result{};
        EXPECT_EQ(
            RackMemShm::GetInstance().UbsMemShmMapMultiNodePool(
                address, 16UL * 1024 * 1024, PROT_READ, MAP_SHARED | MAP_FIXED, "rollback_names", 0, &result, chunk),
            MXM_ERR_MMAP_VA_FAILED);
        EXPECT_EQ(result, nullptr);
        std::vector<std::string> names;
        EXPECT_EQ(ShmMetaDataMgr::GetInstance().GetAllUsedMemoryNames(names), MXM_OK);
        EXPECT_EQ(std::count(names.begin(), names.end(), "rollback_names"), 0);
        EXPECT_EQ(std::count(names.begin(), names.end(), "rollback_names0101"), 0);
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}

TEST_F(RackMemShmTest, TestNameRegistrationFailureRollsBackBeforeLocalMapping)
{
    MOCKER(ShmIpcCommand::IpcCallShmMapMultiNodePool).expects(once()).will(invoke(ReturnPoolMapping));
    MOCKER_CPP(&ShmMetaDataMgr::AddMappingInProgressNames,
               uint32_t(*)(ShmMetaDataMgr *, const std::vector<std::string> &))
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_MALLOC_FAIL)));
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    MOCKER(ObmmOpenInternal).expects(never());
    MOCKER(SystemAdapter::MemoryMap).expects(never());
    void *result{};
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmMapMultiNodePool(nullptr, 16UL * 1024 * 1024, PROT_READ, MAP_SHARED,
                                                                  "registration_fail", 0, &result),
              MXM_ERR_MALLOC_FAIL);
    EXPECT_EQ(result, nullptr);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestPreparationFailuresRollbackOnceAndRemoveMappingInProgressNames)
{
    for (const int stage : {0, 1, 2}) {
        auto response = MakeMultiNodeMapResponse();
        const size_t chunk = stage == 1 ? 3UL * 1024 * 1024 : 0;
        if (stage == 0) {
            response.unitSize_ = 0;
        }
        MOCKER(ShmIpcCommand::IpcCallShmMapMultiNodePool)
            .expects(once())
            .with(any(), any(), any(), outBound(response))
            .will(returnValue(static_cast<uint32_t>(MXM_OK)));
        MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool).expects(once()).will(invoke(FailDetachWithRegisteredNames));
        MOCKER(ObmmOpenInternal).expects(never());
        MOCKER(SystemAdapter::MemoryUnMap).expects(never());
        if (stage == 2) {
            MOCKER(SystemAdapter::MemoryMap).expects(once()).will(returnValue(MAP_FAILED));
        } else {
            MOCKER(SystemAdapter::MemoryMap).expects(never());
        }
        const auto expected = stage == 0 ? MXM_ERR_SHM_POOL_LAYOUT_CONFLICT :
                              stage == 1 ? MXM_ERR_PARAM_INVALID :
                                           MXM_ERR_MMAP_VA_FAILED;
        void *result{};
        EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmMapMultiNodePool(
                      reinterpret_cast<void *>(0x50000000), response.createdSize_, PROT_READ, MAP_SHARED | MAP_FIXED,
                      "prepare_fail", 0, &result, chunk),
                  expected);
        EXPECT_EQ(result, nullptr);
        std::vector<std::string> names;
        EXPECT_EQ(ShmMetaDataMgr::GetInstance().GetAllUsedMemoryNames(names), MXM_OK);
        EXPECT_EQ(std::count(names.begin(), names.end(), "prepare_fail"), 0);
        EXPECT_EQ(std::count(names.begin(), names.end(), "prepare_fail0101"), 0);
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}

TEST_F(RackMemShmTest, TestInterleavedMappingAlternatesEqualProviders)
{
    constexpr size_t mib = 1024UL * 1024;
    MultiNodeShmMapResponse response{MXM_OK, {{11}, {21}}, 8 * mib, 16 * mib, 0, O_RDWR, {8 * mib, 8 * mib}};
    auto metadata = MakeInterleavedMetadata(response, 4 * mib);
    MockInterleavedMapping(2, 4);
    EXPECT_EQ(MapMultiNodeInterleaved(metadata, PROT_READ, MAP_SHARED | MAP_FIXED), MXM_OK);
    EXPECT_EQ(interleavedFdOffsets,
              (std::vector<std::pair<int, off_t>>{{111, 0}, {121, 0}, {111, 4 * mib}, {121, 4 * mib}}));
    EXPECT_TRUE(metadata.addrs.empty());
    EXPECT_EQ(metadata.addrs.capacity(), 0);
    ASSERT_EQ(multiNodeMappedRanges.size(), 4);
    for (size_t index = 0; index < 4; ++index) {
        EXPECT_EQ(multiNodeMappedRanges[index].first, 0x10000000UL + index * 4 * mib);
        EXPECT_EQ(multiNodeMappedRanges[index].second, 4 * mib);
    }
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestInterleavedMappingSkipsExhaustedProviders)
{
    constexpr size_t mib = 1024UL * 1024;
    auto metadata = MakeInterleavedMetadata(MakeMultiNodeMapResponse(), 4 * mib);
    MockInterleavedMapping(3, 4);
    EXPECT_EQ(MapMultiNodeInterleaved(metadata, PROT_READ, MAP_SHARED | MAP_FIXED), MXM_OK);
    EXPECT_EQ(interleavedFdOffsets, (std::vector<std::pair<int, off_t>>{{111, 0}, {121, 0}, {121, 4 * mib}, {122, 0}}));
    EXPECT_TRUE(metadata.addrs.empty());
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestInterleaveChunksCanCrossMemoryIds)
{
    constexpr size_t mib = 1024UL * 1024;
    MultiNodeShmMapResponse response{MXM_OK, {{11, 12, 13}, {21, 22, 23}}, 8 * mib, 48 * mib, 0,
                                     O_RDWR, {24 * mib, 24 * mib}};
    auto metadata = MakeInterleavedMetadata(response, 12 * mib);
    MockInterleavedMapping(6, 8);
    EXPECT_EQ(MapMultiNodeInterleaved(metadata, PROT_READ, MAP_SHARED | MAP_FIXED), MXM_OK);
    EXPECT_EQ(interleavedFdOffsets,
              (std::vector<std::pair<int, off_t>>{
                  {111, 0}, {112, 0}, {121, 0}, {122, 0}, {112, 4 * mib}, {113, 0}, {122, 4 * mib}, {123, 0}}));
    EXPECT_TRUE(metadata.addrs.empty());
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestInterleaveSizeMustDivideEveryProvider)
{
    const auto response = MakeMultiNodeMapResponse();
    EXPECT_EQ(ValidateInterleaveSize(response, 4UL * 1024 * 1024), MXM_OK);
    EXPECT_EQ(ValidateInterleaveSize(response, 8UL * 1024 * 1024), MXM_ERR_PARAM_INVALID);
    EXPECT_EQ(ValidateInterleaveSize(response, 1), MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestInterleavedFailureRollsBackAllOpenedDevices)
{
    multiNodeOpenedIds.clear();
    multiNodeMappedRanges.clear();
    MOCKER(ObmmOpenInternal).expects(exactly(2)).will(invoke(RecordMultiNodeOpen));
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
        .expects(exactly(2))
        .will(invoke(FailSecondMultiNodeMap));
    MOCKER(SystemAdapter::Close).expects(exactly(2)).will(returnValue(0));
    MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    EXPECT_EQ(MapAndSaveMultiNodePool(reinterpret_cast<void *>(0x10000000), 16UL * 1024 * 1024, PROT_READ,
                                      MAP_SHARED | MAP_FIXED, "pool", MakeMultiNodeMapResponse(), 4UL * 1024 * 1024),
              MXM_ERR_MMAP_VA_FAILED);
    GlobalMockObject::verify();
}

static uint32_t ReplyWithMultiNodeMap(const std::string &, uint64_t, int, MultiNodeShmMapResponse &response)
{
    response = MakeMultiNodeMapResponse();
    return MXM_OK;
}

TEST_F(RackMemShmTest, TestInvalidProviderInterleaveSizeRollsBackBeforeAddressReservation)
{
    MOCKER_CPP(&ShmMetaDataMgr::GetAddr, void *(*)(ShmMetaDataMgr *, const std::string &))
        .stubs()
        .will(returnValue(static_cast<void *>(nullptr)));
    MOCKER(ShmIpcCommand::IpcCallShmMapMultiNodePool).expects(once()).will(invoke(ReplyWithMultiNodeMap));
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    MOCKER(SystemAdapter::MemoryMap).expects(never());
    MOCKER(SystemAdapter::MemoryUnMap).expects(never());
    void *result = nullptr;
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmMapMultiNodePool(nullptr, 16UL * 1024 * 1024, PROT_READ, MAP_SHARED,
                                                                  "invalid_interleave", 0, &result, 8UL * 1024 * 1024),
              MXM_ERR_PARAM_INVALID);
    EXPECT_EQ(result, nullptr);
    GlobalMockObject::verify();
}

static std::vector<int> interleavedOwnershipFds;
static std::vector<std::pair<uintptr_t, uintptr_t>> interleavedOwnershipRanges;
static ShmAppMetaData ownershipMetadata;
static size_t ownershipFailureCall{};

static int GetOwnershipMetadata(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &metadata)
{
    metadata = ownershipMetadata;
    return MXM_OK;
}

static int RecordInterleavedOwnership(int fd, void *start, void *end, int prot)
{
    EXPECT_EQ(prot, PROT_READ);
    interleavedOwnershipFds.emplace_back(fd);
    interleavedOwnershipRanges.emplace_back(reinterpret_cast<uintptr_t>(start), reinterpret_cast<uintptr_t>(end));
    return ownershipFailureCall != 0 && interleavedOwnershipFds.size() == ownershipFailureCall ? -1 : 0;
}

static void MockOwnershipMetadata(const ShmAppMetaData &metadata)
{
    MOCKER(ObmmOpenInternal).stubs().will(invoke(RecordMultiNodeOpen));
    MOCKER(SystemAdapter::Close).stubs().will(returnValue(0));
    ownershipMetadata = metadata;
    interleavedOwnershipFds.clear();
    interleavedOwnershipRanges.clear();
    ownershipFailureCall = 0;
    MOCKER_CPP(&ShmMetaDataMgr::GetShmMetaFromName, int32_t(*)(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &))
        .stubs()
        .will(invoke(GetOwnershipMetadata));
    MOCKER_CPP(&ShmMetaDataMgr::CheckAddr, int32_t(*)(ShmMetaDataMgr *, std::string, void *, size_t, ShmAppMetaData &,
                                                      uint64_t &, std::vector<int> &))
        .expects(never());
}

TEST_F(RackMemShmTest, TestOwnershipReopensGroupDeviceAndClosesOnBackendFailure)
{
    auto response = MakeMultiNodeMapResponse();
    response.groupFlags_[1] = UBSM_FLAG_ONLY_IMPORT_NONCACHE;
    response.oflag_ = O_RDONLY;
    auto metadata = MakeInterleavedMetadata(response, 4UL * 1024 * 1024);
    MOCKER(ObmmOpenInternal)
        .expects(once())
        .with(eq(static_cast<mem_id>(22)), eq(static_cast<int>(UBSM_FLAG_ONLY_IMPORT_NONCACHE)), eq(O_RDONLY))
        .will(returnValue(122));
    MOCKER(SystemAdapter::Close).expects(once()).with(eq(122)).will(returnValue(0));
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = [](int, void *, void *, int) {
        return -1;
    };
    const auto base = reinterpret_cast<uintptr_t>(metadata.addr);
    EXPECT_EQ(SwitchOwnershipRange(metadata, 2, base, base + FOUR_KB, PROT_READ), MXM_ERR_OBMM_INNER);
    executor.obmmOwnershipSwitch = previous;
    EXPECT_EQ(temporaryObmmFds.load(), 0u);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestInterleavedOwnershipUsesSegmentDeviceAndRange)
{
    constexpr size_t mib = 1024UL * 1024;
    constexpr uintptr_t base = 0x10000000;
    auto metadata = MakeInterleavedMetadata(MakeMultiNodeMapResponse(), 4 * mib);
    MockOwnershipMetadata(metadata);
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = RecordInterleavedOwnership;
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, reinterpret_cast<void *>(base + 2 * mib),
                                                              8 * mib, READ),
              MXM_OK);
    executor.obmmOwnershipSwitch = previous;
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{111, 121, 121}));
    EXPECT_EQ(
        interleavedOwnershipRanges,
        (std::vector<std::pair<uintptr_t, uintptr_t>>{
            {base + 2 * mib, base + 4 * mib}, {base + 4 * mib, base + 8 * mib}, {base + 8 * mib, base + 10 * mib}}));
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestInterleavedOwnershipSplitsChunksAcrossMemoryIds)
{
    constexpr size_t mib = 1024UL * 1024;
    constexpr uintptr_t base = 0x10000000;
    MultiNodeShmMapResponse response{MXM_OK, {{11, 12, 13}, {21, 22, 23}}, 8 * mib, 48 * mib, 0,
                                     O_RDWR, {24 * mib, 24 * mib}};
    auto metadata = MakeInterleavedMetadata(response, 12 * mib);
    MockOwnershipMetadata(metadata);
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = RecordInterleavedOwnership;
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, reinterpret_cast<void *>(base + 7 * mib),
                                                              22 * mib, READ),
              MXM_OK);
    executor.obmmOwnershipSwitch = previous;
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{111, 112, 121, 122, 112, 113}));
    EXPECT_EQ(interleavedOwnershipRanges,
              (std::vector<std::pair<uintptr_t, uintptr_t>>{{base + 7 * mib, base + 8 * mib},
                                                            {base + 8 * mib, base + 12 * mib},
                                                            {base + 12 * mib, base + 20 * mib},
                                                            {base + 20 * mib, base + 24 * mib},
                                                            {base + 24 * mib, base + 28 * mib},
                                                            {base + 28 * mib, base + 29 * mib}}));
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestInterleavedOwnershipStartsAfterProviderExhaustionAndStopsOnFailure)
{
    constexpr size_t mib = 1024UL * 1024;
    constexpr uintptr_t base = 0x10000000;
    auto metadata = MakeInterleavedMetadata(MakeMultiNodeMapResponse(), 4 * mib);
    MockOwnershipMetadata(metadata);
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = RecordInterleavedOwnership;
    auto &shm = RackMemShm::GetInstance();
    EXPECT_EQ(shm.UbsMemShmSetOwnerShip(metadata.name, reinterpret_cast<void *>(base + 9 * mib), 6 * mib, READ),
              MXM_OK);
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{121, 122}));
    EXPECT_EQ(interleavedOwnershipRanges, (std::vector<std::pair<uintptr_t, uintptr_t>>{
                                              {base + 9 * mib, base + 12 * mib}, {base + 12 * mib, base + 15 * mib}}));
    interleavedOwnershipFds.clear();
    interleavedOwnershipRanges.clear();
    ownershipFailureCall = 2;
    EXPECT_EQ(shm.UbsMemShmSetOwnerShip(metadata.name, metadata.addr, metadata.mapSize, READ), MXM_ERR_OBMM_INNER);
    executor.obmmOwnershipSwitch = previous;
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{111, 121}));
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestTinyOwnershipRangeAtEndOfHugeInterleavedPool)
{
    constexpr size_t tib = 1UL << 40;
    constexpr uintptr_t base = 0x10000000;
    MultiNodeShmMapResponse response{MXM_OK, {{11}, {21}}, tib, 2 * tib, 0, O_RDWR, {tib, tib}};
    auto metadata = MakeInterleavedMetadata(response, FOUR_KB);
    MockOwnershipMetadata(metadata);
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = RecordInterleavedOwnership;
    auto start = reinterpret_cast<void *>(base + metadata.mapSize - FOUR_KB);
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, start, FOUR_KB, READ), MXM_OK);
    executor.obmmOwnershipSwitch = previous;
    EXPECT_TRUE(metadata.addrs.empty());
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{121}));
    EXPECT_EQ(
        interleavedOwnershipRanges,
        (std::vector<std::pair<uintptr_t, uintptr_t>>{{base + metadata.mapSize - FOUR_KB, base + metadata.mapSize}}));
    GlobalMockObject::verify();
}

static size_t peakTemporaryDevices;

static uint32_t CheckTemporaryInterleavedMap(RackMemFdMap *, void *, size_t, int, int, int, off_t)
{
    peakTemporaryDevices = std::max(peakTemporaryDevices, static_cast<size_t>(temporaryObmmFds.load()));
    return MXM_OK;
}

TEST_F(RackMemShmTest, TestInterleavedDescriptorsAreBoundedByProviderCount)
{
    constexpr size_t mib = 1024UL * 1024;
    MultiNodeShmMapResponse response{MXM_OK, {{11, 12, 13}, {21, 22, 23}}, 4 * mib, 24 * mib, 0,
                                     O_RDWR, {12 * mib, 12 * mib}};
    auto metadata = MakeInterleavedMetadata(response, 2 * mib);
    peakTemporaryDevices = 0;
    MOCKER(ObmmOpenInternal).expects(exactly(6)).will(invoke(RecordMultiNodeOpen));
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
        .expects(exactly(12))
        .will(invoke(CheckTemporaryInterleavedMap));
    MOCKER(SystemAdapter::Close).expects(exactly(6)).will(returnValue(0));
    EXPECT_EQ(MapMultiNodeInterleaved(metadata, PROT_READ, MAP_SHARED | MAP_FIXED), MXM_OK);
    EXPECT_EQ(peakTemporaryDevices, 2u);
    EXPECT_EQ(temporaryObmmFds.load(), 0u);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestInterleavedOpenFailureClosesCurrentProviderDevices)
{
    constexpr size_t mib = 1024UL * 1024;
    auto metadata = MakeInterleavedMetadata(MakeMultiNodeMapResponse(), 2 * mib);
    MOCKER(ObmmOpenInternal).expects(exactly(2)).will(returnValue(111)).then(returnValue(-1));
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    MOCKER(SystemAdapter::Close).expects(once()).with(eq(111)).will(returnValue(0));
    EXPECT_EQ(MapMultiNodeInterleaved(metadata, PROT_READ, MAP_SHARED | MAP_FIXED), MXM_ERR_SHM_OBMM_OPEN);
    EXPECT_EQ(temporaryObmmFds.load(), 0u);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestInterleavedMetadataSizeDoesNotScaleWithChunkCount)
{
    constexpr size_t mib = 1024UL * 1024;
    MultiNodeShmMapResponse response{MXM_OK, {{11}, {21}}, 8 * mib, 16 * mib, 0, O_RDWR, {8 * mib, 8 * mib}};
    auto metadata = MakeInterleavedMetadata(response, FOUR_KB);
    MOCKER(ObmmOpenInternal).expects(exactly(2)).will(returnValue(111)).then(returnValue(121));
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
        .expects(exactly(metadata.mapSize / FOUR_KB))
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    MOCKER(SystemAdapter::Close).expects(exactly(2)).will(returnValue(0));
    EXPECT_EQ(MapMultiNodeInterleaved(metadata, PROT_READ, MAP_SHARED | MAP_FIXED), MXM_OK);
    EXPECT_TRUE(metadata.addrs.empty());
    EXPECT_EQ(metadata.addrs.capacity(), 0);
    EXPECT_TRUE(metadata.slices.empty());
    EXPECT_EQ(temporaryObmmFds.load(), 0u);
    GlobalMockObject::verify();
}

static ShmAppMetaData MakeShortSegmentOwnershipMetadata(bool multiNode)
{
    constexpr size_t mib = 1024UL * 1024;
    ShmAppMetaData metadata{"ownership_pool", {11, 21}, reinterpret_cast<void *>(0x10000000), 8 * mib, READWRITE, 0};
    metadata.unitSize = multiNode ? 8 * mib : 4 * mib;
    metadata.addrs = {{metadata.addr, 4 * mib}, {reinterpret_cast<void *>(0x10400000), 4 * mib}};
    for (size_t index = 0; index < metadata.memIds.size(); ++index) {
        metadata.slices.push_back({metadata.memIds[index], metadata.addrs[index].first, 4 * mib, 0, 0, O_RDWR});
    }
    if (multiNode) {
        metadata.groupMemIds = {{11}, {21}};
        metadata.groupSizes = {4 * mib, 4 * mib};
        metadata.groupFlags = {0, 0};
    }
    return metadata;
}

TEST_F(RackMemShmTest, TestConsecutiveOwnershipUsesShortSegmentsInsteadOfUnitIndices)
{
    constexpr size_t mib = 1024UL * 1024;
    auto metadata = MakeShortSegmentOwnershipMetadata(true);
    MockOwnershipMetadata(metadata);
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = RecordInterleavedOwnership;
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, metadata.addr, metadata.mapSize, READ),
              MXM_OK);
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{111, 121}));
    interleavedOwnershipFds.clear();
    interleavedOwnershipRanges.clear();
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, reinterpret_cast<void *>(0x10500000),
                                                              2 * mib, READ),
              MXM_OK);
    executor.obmmOwnershipSwitch = previous;
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{121}));
    EXPECT_EQ(interleavedOwnershipRanges, (std::vector<std::pair<uintptr_t, uintptr_t>>{{0x10500000, 0x10700000}}));
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestOriginalNoncacheFlagsRejectOwnershipForAllLayouts)
{
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = RecordInterleavedOwnership;
    for (const uint64_t flag : {UBSM_FLAG_ONLY_IMPORT_NONCACHE, UBSM_FLAG_NONCACHE}) {
        for (const int layout : {0, 1, 2}) {
            auto metadata = MakeShortSegmentOwnershipMetadata(layout != 0);
            metadata.flag = flag;
            metadata.groupFlags = {flag, 0}; // The last local group must not erase the original NC mode.
            if (layout == 2) {
                constexpr size_t chunk = 2UL * 1024 * 1024;
                metadata.interleaveSize = chunk;
                metadata.addrs.clear();
            }
            MockOwnershipMetadata(metadata);
            for (const auto status : {READ, READWRITE, UNACCESS}) {
                EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, metadata.addr,
                                                                          metadata.mapSize, status),
                          MXM_ERR_PARAM_INVALID);
                EXPECT_EQ(
                    RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, metadata.addr, FOUR_KB, status),
                    MXM_ERR_PARAM_INVALID);
            }
            EXPECT_TRUE(interleavedOwnershipFds.empty());
            GlobalMockObject::verify();
            GlobalMockObject::reset();
        }
    }
    executor.obmmOwnershipSwitch = previous;
}

TEST_F(RackMemShmTest, TestNoncacheOwnershipStillRejectsInvalidRangesAndRevokedMappings)
{
    auto metadata = MakeShortSegmentOwnershipMetadata(true);
    metadata.flag = UBSM_FLAG_ONLY_IMPORT_NONCACHE;
    MockOwnershipMetadata(metadata);
    auto &shm = RackMemShm::GetInstance();
    EXPECT_EQ(shm.UbsMemShmSetOwnerShip(metadata.name, metadata.addr, metadata.mapSize + FOUR_KB, READ),
              MXM_ERR_PARAM_INVALID);
    EXPECT_EQ(shm.UbsMemShmSetOwnerShip(metadata.name, reinterpret_cast<void *>(0x10000001), FOUR_KB, READ),
              MXM_ERR_PARAM_INVALID);
    EXPECT_EQ(shm.UbsMemShmSetOwnerShip(metadata.name, metadata.addr, 1, READ), MXM_ERR_PARAM_INVALID);
    ownershipMetadata.SetMappingRevoked(true);
    EXPECT_EQ(shm.UbsMemShmSetOwnerShip(metadata.name, metadata.addr, metadata.mapSize, READ), MXM_ERR_SHM_NOT_FOUND);
    ownershipMetadata.SetMappingRevoked(false);
    ownershipMetadata.addressReleased = true;
    EXPECT_EQ(shm.UbsMemShmSetOwnerShip(metadata.name, metadata.addr, metadata.mapSize, READ), MXM_ERR_SHM_NOT_FOUND);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestOrdinaryOwnershipUsesActualRangeAndPropagatesFailure)
{
    auto metadata = MakeShortSegmentOwnershipMetadata(false);
    MockOwnershipMetadata(metadata);
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = RecordInterleavedOwnership;
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, metadata.addr, FOUR_KB, READ), MXM_OK);
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{111}));
    EXPECT_EQ(interleavedOwnershipRanges,
              (std::vector<std::pair<uintptr_t, uintptr_t>>{{0x10000000, 0x10000000 + FOUR_KB}}));
    interleavedOwnershipFds.clear();
    interleavedOwnershipRanges.clear();
    ownershipFailureCall = 2;
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, metadata.addr, metadata.mapSize, READ),
              MXM_ERR_OBMM_INNER);
    executor.obmmOwnershipSwitch = previous;
    EXPECT_EQ(interleavedOwnershipFds, (std::vector<int>{111, 121}));
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestBothLayoutsSaveOriginalFlagsSeparatelyFromEffectiveGroupFlags)
{
    auto &manager = ShmMetaDataMgr::GetInstance();
    for (const size_t chunk : {0UL, 4UL * 1024 * 1024}) {
        auto response = MakeMultiNodeMapResponse();
        response.flag_ = UBSM_FLAG_ONLY_IMPORT_NONCACHE;
        response.groupFlags_ = {UBSM_FLAG_ONLY_IMPORT_NONCACHE, 0};
        MockInterleavedMapping(3, chunk == 0 ? 3 : 4);
        auto address = reinterpret_cast<void *>(0x50000000);
        ASSERT_EQ(MapAndSaveMultiNodePool(address, response.createdSize_, PROT_READ, MAP_SHARED | MAP_FIXED,
                                          "original_flag_pool", response, chunk),
                  MXM_OK);
        ShmAppMetaData saved;
        ASSERT_EQ(manager.GetShmMetaFromName("original_flag_pool", saved), MXM_OK);
        EXPECT_EQ(saved.flag, response.flag_);
        EXPECT_EQ(saved.groupFlags, response.groupFlags_);
        EXPECT_EQ(manager.RemoveMetaData(address, saved.name), MXM_OK);
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}

TEST_F(RackMemShmTest, TestOwnershipValidatesAllMemoryIdsBeforeCallingBackend)
{
    auto metadata = MakeShortSegmentOwnershipMetadata(true);
    metadata.interleaveSize = 4UL * 1024 * 1024;
    metadata.addrs.clear();
    metadata.memIds.pop_back();
    MockOwnershipMetadata(metadata);
    auto &executor = RmLibObmmExecutor::GetInstance();
    const auto previous = executor.obmmOwnershipSwitch;
    executor.obmmOwnershipSwitch = RecordInterleavedOwnership;
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmSetOwnerShip(metadata.name, metadata.addr, metadata.mapSize, READ),
              MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
    executor.obmmOwnershipSwitch = previous;
    EXPECT_TRUE(interleavedOwnershipFds.empty());
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestCompactInterleavedOwnershipRejectsInvalidLayouts)
{
    auto valid = MakeInterleavedMetadata(MakeMultiNodeMapResponse(), 4UL * 1024 * 1024);
    EXPECT_EQ(ValidateOwnershipMapping(valid, valid.addr, valid.mapSize), MXM_OK);
    std::vector<ShmAppMetaData> invalid(10, valid);
    invalid[0].unitSize = 0;
    invalid[1].interleaveSize = FOUR_KB + 1;
    invalid[2].groupSizes.pop_back();
    invalid[3].groupSizes[0] += FOUR_KB;
    invalid[4].groupSizes[0] = 0;
    invalid[5].groupMemIds[1].pop_back();
    invalid[6].memIds.pop_back();
    invalid[7].memIds.push_back(123);
    invalid[8].groupFlags.clear();
    invalid[9].addrs.emplace_back(valid.addr, valid.mapSize);
    for (const auto &metadata : invalid) {
        EXPECT_EQ(ValidateOwnershipMapping(metadata, metadata.addr, metadata.mapSize),
                  MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
    }
    valid.flag = UBSM_FLAG_NONCACHE;
    valid.SetMappingRevoked(true);
    EXPECT_EQ(ValidateOwnershipMapping(valid, valid.addr, valid.mapSize), MXM_ERR_SHM_NOT_FOUND);
}

TEST_F(RackMemShmTest, TestMultiNodeMappingFollowsHostOrderWithoutGroupPadding)
{
    const auto response = MakeMultiNodeMapResponse();
    constexpr uintptr_t base = 0x10000000;
    constexpr size_t mib = 1024UL * 1024;
    ShmAppMetaData metadata{"multi_pool", {11, 21, 22}, reinterpret_cast<void *>(base), 16 * mib, READWRITE, 0};
    metadata.unitSize = response.unitSize_;
    metadata.oflag = response.oflag_;
    metadata.groupMemIds = response.memIds_;
    metadata.groupSizes = response.groupSizes_;
    metadata.groupFlags = response.groupFlags_;
    multiNodeOpenedIds.clear();
    multiNodeMappedRanges.clear();
    MOCKER(ObmmOpenInternal).expects(exactly(3)).will(invoke(RecordMultiNodeOpen));
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
        .expects(exactly(3))
        .will(invoke(RecordMultiNodeMap));
    MOCKER(SystemAdapter::Close).stubs().will(returnValue(0));
    EXPECT_EQ(MapMultiNodeGroups(metadata, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED), MXM_OK);
    EXPECT_EQ(multiNodeOpenedIds, (std::vector<uint64_t>{11, 21, 22}));
    EXPECT_EQ(multiNodeMappedRanges, (std::vector<std::pair<uintptr_t, size_t>>{
                                         {base, 4 * mib}, {base + 4 * mib, 8 * mib}, {base + 12 * mib, 4 * mib}}));
    EXPECT_EQ(metadata.slices.size(), 3);
    EXPECT_EQ(temporaryObmmFds.load(), 0u);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestMultiNodeMapValidatesGroupSizesAndMemIdCounts)
{
    auto response = MakeMultiNodeMapResponse();
    EXPECT_EQ(ValidateMultiNodeMapResponse(response, 16UL * 1024 * 1024), MXM_OK);
    response.groupSizes_.pop_back();
    EXPECT_EQ(ValidateMultiNodeMapResponse(response, 16UL * 1024 * 1024), MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
    response = MakeMultiNodeMapResponse();
    response.memIds_[1].pop_back();
    EXPECT_EQ(ValidateMultiNodeMapResponse(response, 16UL * 1024 * 1024), MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
    response = MakeMultiNodeMapResponse();
    EXPECT_EQ(ValidateMultiNodeMapResponse(response, 8UL * 1024 * 1024), MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
}

TEST_F(RackMemShmTest, TestMultiNodeMappingFailureReleasesFdsAddressAndAttach)
{
    multiNodeOpenedIds.clear();
    multiNodeMappedRanges.clear();
    MOCKER(ObmmOpenInternal).expects(exactly(2)).will(invoke(RecordMultiNodeOpen));
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
        .expects(exactly(2))
        .will(invoke(FailSecondMultiNodeMap));
    MOCKER(SystemAdapter::Close).expects(exactly(2)).will(returnValue(0));
    MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    const auto response = MakeMultiNodeMapResponse();
    EXPECT_EQ(MapAndSaveMultiNodePool(reinterpret_cast<void *>(0x10000000), 16UL * 1024 * 1024, PROT_READ,
                                      MAP_SHARED | MAP_FIXED, "multi_pool", response),
              MXM_ERR_MMAP_VA_FAILED);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestMultiNodeNoReplaceReservationFailureDoesNotUnmapExistingAddress)
{
    auto response = MakeMultiNodeMapResponse();
    void *address = nullptr;
    MOCKER(SystemAdapter::MemoryMap).expects(once()).will(returnValue(MAP_FAILED));
    MOCKER(SystemAdapter::MemoryUnMap).expects(never());
    EXPECT_EQ(PrepareMultiNodeMappingAddress(RackMemShm::GetInstance(), reinterpret_cast<void *>(0x10000000),
                                             16UL * 1024 * 1024, MAP_SHARED | MAP_FIXED_NOREPLACE, response, address),
              MXM_ERR_MMAP_VA_FAILED);
    EXPECT_EQ(address, nullptr);
    GlobalMockObject::verify();
}

static int GetMultiNodeNameForUnmap(ShmMetaDataMgr *, void *, std::string &name)
{
    name = "multi_pool";
    return MXM_OK;
}

static int GetMultiNodeMetadataForUnmap(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &metadata)
{
    const size_t size = 16UL * 1024 * 1024;
    metadata.name = "multi_pool";
    metadata.addr = reinterpret_cast<void *>(0x10000000);
    metadata.mapSize = size;
    metadata.groupMemIds = {{11}, {21}};
    metadata.groupSizes = {size / 2, size / 2};
    return MXM_OK;
}

TEST_F(RackMemShmTest, TestUnmapMultiNodePoolUsesMultiNodeIpc)
{
    void *address = reinterpret_cast<void *>(0x10000000);
    MOCKER_CPP(&ShmMetaDataMgr::GetNameByAddr, int32_t(*)(ShmMetaDataMgr *, void *, std::string &))
        .stubs()
        .will(invoke(GetMultiNodeNameForUnmap));
    MOCKER_CPP(&ShmMetaDataMgr::GetShmMetaFromName, int32_t(*)(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &))
        .stubs()
        .will(invoke(GetMultiNodeMetadataForUnmap));
    MOCKER(SystemAdapter::MemoryMap).stubs().will(returnValue(address));
    MOCKER_CPP(&ShmMetaDataMgr::UpdateMetaData, int (*)(ShmMetaDataMgr *, const std::string &, const ShmAppMetaData &))
        .stubs()
        .will(returnValue(static_cast<int>(MXM_OK)));
    MOCKER_CPP(&ShmMetaDataMgr::RemoveMetaData, uint32_t(*)(ShmMetaDataMgr *, void *, const std::string &))
        .stubs()
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    MOCKER(ShmIpcCommand::IpcCallShmUnMap).expects(never());
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmUnmmap(address, 16UL * 1024 * 1024), MXM_OK);
    GlobalMockObject::verify();
}

static ShmAppMetaData pendingUnmapMetadata;

static int GetPendingUnmapName(ShmMetaDataMgr *, void *, std::string &name)
{
    name = pendingUnmapMetadata.name;
    return MXM_OK;
}

static int GetPendingUnmapMetadata(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &metadata)
{
    metadata = pendingUnmapMetadata;
    return MXM_OK;
}

static int SavePendingUnmapMetadata(ShmMetaDataMgr *, const std::string &, const ShmAppMetaData &metadata)
{
    pendingUnmapMetadata = metadata;
    return MXM_OK;
}

static void MockPendingMultiNodeUnmap(size_t chunk)
{
    MOCKER_CPP(madvise, int (*)(void *, size_t, int)).stubs().will(returnValue(0));
    pendingUnmapMetadata = MakeInterleavedMetadata(MakeMultiNodeMapResponse(), chunk);
    MOCKER_CPP(&ShmMetaDataMgr::GetNameByAddr, int32_t(*)(ShmMetaDataMgr *, void *, std::string &))
        .stubs()
        .will(invoke(GetPendingUnmapName));
    MOCKER_CPP(&ShmMetaDataMgr::GetShmMetaFromName, int32_t(*)(ShmMetaDataMgr *, const std::string &, ShmAppMetaData &))
        .stubs()
        .will(invoke(GetPendingUnmapMetadata));
    MOCKER_CPP(&ShmMetaDataMgr::UpdateMetaData, int (*)(ShmMetaDataMgr *, const std::string &, const ShmAppMetaData &))
        .stubs()
        .will(invoke(SavePendingUnmapMetadata));
    MOCKER(SystemAdapter::MemoryMap).expects(once()).will(returnValue(pendingUnmapMetadata.addr));
    MOCKER(SystemAdapter::Close).expects(never());
    MOCKER(ObmmOpenInternal).expects(never());
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(RackMemFdMap *, void *, size_t, int, int, int, off_t))
        .expects(never());
    MOCKER(ShmIpcCommand::IpcCallShmUnMap).expects(never());
}

TEST_F(RackMemShmTest, TestBothMultiNodeLayoutsRetryUnmapWithoutRemapping)
{
    for (const size_t chunk : {0UL, 4UL * 1024 * 1024}) {
        MockPendingMultiNodeUnmap(chunk);
        MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
            .expects(exactly(2))
            .will(returnValue(static_cast<uint32_t>(MXM_ERR_IPC_SYNC_CALL)))
            .then(returnValue(static_cast<uint32_t>(MXM_OK)));
        MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
        MOCKER_CPP(&ShmMetaDataMgr::RemoveMetaData, uint32_t(*)(ShmMetaDataMgr *, void *, const std::string &))
            .expects(once())
            .will(returnValue(static_cast<uint32_t>(MXM_OK)));
        auto &shm = RackMemShm::GetInstance();
        EXPECT_EQ(shm.UbsMemShmUnmmap(pendingUnmapMetadata.addr, pendingUnmapMetadata.mapSize), MXM_ERR_IPC_SYNC_CALL);
        EXPECT_TRUE(pendingUnmapMetadata.IsMappingRevoked());
        EXPECT_FALSE(pendingUnmapMetadata.detachCompleted);
        EXPECT_FALSE(pendingUnmapMetadata.addressReleased);
        EXPECT_EQ(temporaryObmmFds.load(), 0u);
        EXPECT_EQ(shm.UbsMemShmUnmmap(pendingUnmapMetadata.addr, pendingUnmapMetadata.mapSize), MXM_OK);
        EXPECT_TRUE(pendingUnmapMetadata.detachCompleted);
        EXPECT_TRUE(pendingUnmapMetadata.addressReleased);
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}

TEST_F(RackMemShmTest, TestUnmapAddressFailureKeepsMetadataAndSkipsDetachOnRetry)
{
    MockPendingMultiNodeUnmap(4UL * 1024 * 1024);
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    MOCKER(SystemAdapter::MemoryUnMap).expects(exactly(2)).will(returnValue(-1)).then(returnValue(0));
    MOCKER_CPP(&ShmMetaDataMgr::RemoveMetaData, uint32_t(*)(ShmMetaDataMgr *, void *, const std::string &))
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    auto &shm = RackMemShm::GetInstance();
    EXPECT_EQ(shm.UbsMemShmUnmmap(pendingUnmapMetadata.addr, pendingUnmapMetadata.mapSize), MXM_ERR_MMAP_VA_FAILED);
    EXPECT_TRUE(pendingUnmapMetadata.detachCompleted);
    EXPECT_FALSE(pendingUnmapMetadata.addressReleased);
    EXPECT_EQ(shm.UbsMemShmUnmmap(pendingUnmapMetadata.addr, pendingUnmapMetadata.mapSize), MXM_OK);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestUnmapMetadataRetryDoesNotTouchReleasedAddress)
{
    MockPendingMultiNodeUnmap(0);
    MOCKER(ShmIpcCommand::IpcCallShmUnmapMultiNodePool)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(MXM_OK)));
    MOCKER(SystemAdapter::MemoryUnMap).expects(once()).will(returnValue(0));
    MOCKER_CPP(&ShmMetaDataMgr::RemoveMetaData, uint32_t(*)(ShmMetaDataMgr *, void *, const std::string &))
        .expects(exactly(2))
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_MEMLIB)))
        .then(returnValue(static_cast<uint32_t>(MXM_OK)));
    auto &shm = RackMemShm::GetInstance();
    EXPECT_EQ(shm.UbsMemShmUnmmap(pendingUnmapMetadata.addr, pendingUnmapMetadata.mapSize), MXM_ERR_MEMLIB);
    EXPECT_TRUE(pendingUnmapMetadata.addressReleased);
    EXPECT_EQ(shm.UbsMemShmUnmmap(pendingUnmapMetadata.addr, pendingUnmapMetadata.mapSize), MXM_OK);
    GlobalMockObject::verify();
}

TEST_F(RackMemShmTest, TestUbsMemShmCreateParameterCheckFailure)
{
    std::string invalidNid = "invalidNid";
    SHMRegionDesc mockRegionDesc = {};
    MOCKER(RackMemShm::CheckRegionPar)
        .stubs()
        .with(eq(invalidNid), any())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_MALLOC_FAIL)));

    uint32_t result =
        RackMemShm::GetInstance().UbsMemShmCreate("region", "name", 1024, invalidNid, mockRegionDesc, 0, 0644);
    EXPECT_EQ(result, MXM_ERR_MALLOC_FAIL);
}

TEST_F(RackMemShmTest, TestUbsMemShmCreateIpcCallFailure)
{
    std::string validNid = "validNid";
    SHMRegionDesc mockRegionDesc = {};
    const uint32_t expectedError = MXM_ERR_MALLOC_FAIL;

    MOCKER(RackMemShm::CheckRegionPar)
        .stubs()
        .with(eq(validNid), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    MOCKER(ShmIpcCommand::IpcCallShmCreate)
        .stubs()
        .with(any(), any(), any(), any(), any(), any(), any())
        .will(returnValue(expectedError));

    uint32_t result =
        RackMemShm::GetInstance().UbsMemShmCreate("region", "name", 1024, validNid, mockRegionDesc, 0, 0644);
    EXPECT_EQ(result, expectedError);
}

TEST_F(RackMemShmTest, TestUbsMemShmCreateSuccess)
{
    std::string validNid = "validNid";
    SHMRegionDesc mockRegionDesc = {};
    MOCKER(RackMemShm::CheckRegionPar)
        .stubs()
        .with(eq(validNid), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    MOCKER(ShmIpcCommand::IpcCallShmCreate)
        .stubs()
        .with(any(), any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    uint32_t result =
        RackMemShm::GetInstance().UbsMemShmCreate("region", "name", 1024, validNid, mockRegionDesc, 0, 0644);
    EXPECT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestUbsMemShmCreateMultiNodePoolSuccess)
{
    MOCKER(ShmIpcCommand::IpcCallShmCreateMultiNodePool)
        .stubs()
        .with(any(), any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    const std::vector<std::string> hostNames{"host1", "host2"};
    const auto result = RackMemShm::GetInstance().UbsMemShmCreateMultiNodePool(validName, 8UL * 1024 * 1024, 0644,
                                                                               UBSM_FLAG_CACHE, 0, hostNames, {});
    EXPECT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestUbsMemShmCreateMultiNodePoolFailure)
{
    MOCKER(ShmIpcCommand::IpcCallShmCreateMultiNodePool)
        .stubs()
        .with(any(), any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_UBSE_INNER)));

    const std::vector<std::string> hostNames{"host1", "host2"};
    const auto result = RackMemShm::GetInstance().UbsMemShmCreateMultiNodePool(validName, 8UL * 1024 * 1024, 0644,
                                                                               UBSM_FLAG_CACHE, 0, hostNames, {});
    EXPECT_EQ(result, MXM_ERR_UBSE_INNER);
}

TEST_F(RackMemShmTest, TestUbsMemShmDeleteMultiNodePoolSuccess)
{
    MOCKER(ShmIpcCommand::IpcCallShmDeleteMultiNodePool)
        .stubs()
        .with(eq(validName))
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmDeleteMultiNodePool(validName), UBSM_OK);
}

TEST_F(RackMemShmTest, TestUbsMemShmDeleteMultiNodePoolFailure)
{
    MOCKER(ShmIpcCommand::IpcCallShmDeleteMultiNodePool)
        .stubs()
        .with(eq(validName))
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_UBSE_INNER)));

    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmDeleteMultiNodePool(validName), MXM_ERR_UBSE_INNER);
}

TEST_F(RackMemShmTest, TestUnMapWhenErrorCallsCleanupFunctions)
{
    MOCKER(SystemAdapter::MemoryUnMap).stubs().with(any(), any()).will(returnValue(-1));

    MOCKER(ShmIpcCommand::IpcCallShmUnMap)
        .stubs()
        .with(eq(validName))
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_MALLOC_FAIL)));

    shmMetaData.AddAddr(validAddr, validSize);
    shmMetaData.slices.push_back({1, validAddr, validSize, 0, 0, O_RDWR});

    UnMapWhenError(validAddr, validSize, validName, shmMetaData);

    EXPECT_TRUE(shmMetaData.addr == nullptr);
    EXPECT_EQ(shmMetaData.slices.size(), 1U);
}

TEST_F(RackMemShmTest, TestParameterCheckNameAlreadyMapped)
{
    MOCKER_CPP(&ShmMetaDataMgr::GetAddr, void *(*)(const std::string &name))
        .stubs()
        .with(any())
        .will(returnValue(validAddr));

    uint32_t result = RackMemShmMmapParameterCheck(nullptr, validSize, PROT_READ, MAP_SHARED, validName, 0);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestParameterCheckZeroSize)
{
    uint32_t result = RackMemShmMmapParameterCheck(nullptr, 0, PROT_READ, MAP_SHARED, validName, 0);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestParameterCheckNonZeroOffset)
{
    uint32_t result = RackMemShmMmapParameterCheck(nullptr, validSize, PROT_READ, MAP_SHARED, validName, 1);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestParameterCheckInvalidProt)
{
    uint32_t result = RackMemShmMmapParameterCheck(validAddr, validSize, PROT_EXEC, MAP_SHARED, validName, 0);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestParameterCheckInvalidFlags)
{
    uint32_t result = RackMemShmMmapParameterCheck(nullptr, validSize, PROT_READ, MAP_FIXED, validName, 0);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestParameterCheckSuccess)
{
    uint32_t result = RackMemShmMmapParameterCheck(nullptr, validSize, PROT_READ, MAP_SHARED, validName, 0);
    EXPECT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestMmapByMemIDOpenFailure)
{
    MOCKER(ObmmOpenInternal).stubs().with(any(), any(), any()).will(returnValue(-1));

    uint32_t result =
        RackMemShmMmapByMemID(validAddr, validSize, 2, memIds, shmMetaData, O_RDWR, 0, PROT_READ, MAP_SHARED);
    EXPECT_EQ(result, MXM_ERR_SHM_OBMM_OPEN);
}

TEST_F(RackMemShmTest, TestMmapByMemIDMapFailure)
{
    MOCKER(SystemAdapter::Close).stubs().will(returnValue(0));
    MOCKER(ObmmOpenInternal).stubs().with(any(), any(), any()).will(returnValue(123));

    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(void *, size_t, int, int, int, off_t))
        .stubs()
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_MMAP_VA_FAILED)));

    uint32_t result =
        RackMemShmMmapByMemID(validAddr, validSize, 2, memIds, shmMetaData, O_RDWR, 0, PROT_READ, MAP_SHARED);
    EXPECT_EQ(result, MXM_ERR_MMAP_VA_FAILED);
}

TEST_F(RackMemShmTest, TestMmapByMemIDLeavesAddressCleanupToCaller)
{
    MOCKER(SystemAdapter::Close).stubs().will(returnValue(0));
    shmMetaData.mapSize = validSize * memIds.size();
    MOCKER(ObmmOpenInternal).stubs().with(any(), any(), any()).will(returnValue(123));
    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(void *, size_t, int, int, int, off_t))
        .stubs()
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)))
        .then(returnValue(static_cast<uint32_t>(MXM_ERR_MMAP_VA_FAILED)));
    MOCKER(SystemAdapter::MemoryUnMap).expects(never());

    auto result = RackMemShmMmapByMemID(validAddr, validSize, 2, memIds, shmMetaData, O_RDWR, 0, PROT_READ, MAP_SHARED);

    EXPECT_EQ(result, MXM_ERR_MMAP_VA_FAILED);
}

TEST_F(RackMemShmTest, TestMmapByMemIDSuccess)
{
    MOCKER(SystemAdapter::Close).stubs().will(returnValue(0));
    MOCKER(ObmmOpenInternal).stubs().with(any(), any(), any()).will(returnValue(123));

    MOCKER_CPP(&RackMemFdMap::MapForEachFd, uint32_t(*)(void *, size_t, int, int, int, off_t))
        .stubs()
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    uint32_t result =
        RackMemShmMmapByMemID(validAddr, validSize, 2, memIds, shmMetaData, O_RDWR, 0, PROT_READ, MAP_SHARED);
    EXPECT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestGetOwnStatusAllCases)
{
    EXPECT_EQ(GetOwnStatus(PROT_NONE), ShmOwnStatus::UNACCESS);
    EXPECT_EQ(GetOwnStatus(PROT_READ), ShmOwnStatus::READ);
    EXPECT_EQ(GetOwnStatus(PROT_WRITE), ShmOwnStatus::WRITE);
    EXPECT_EQ(GetOwnStatus(PROT_READ | PROT_WRITE), ShmOwnStatus::READWRITE);
    EXPECT_EQ(GetOwnStatus(PROT_EXEC), OWNBUFF);
}

TEST_F(RackMemShmTest, UnsupportedResourceFlagsNeverReachDeviceMapping)
{
    const size_t size = 4096;
    const size_t unit = 4096;
    const uint64_t unsupported = UINT64_C(1) << 63;
    const int oflag = O_RDWR;
    MOCKER(RackMemShmMmapParameterCheck).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(ShmIpcCommand::IpcCallShmMap)
        .stubs()
        .with(any(), any(), outBound(memIds), outBound(size), outBound(unit), outBound(unsupported), outBound(oflag),
              any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(ShmIpcCommand::IpcCallShmUnMap).expects(once()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(SystemAdapter::MemoryMap).expects(never());
    void *address = nullptr;
    EXPECT_EQ(RackMemShm::GetInstance().UbsMemShmMmap(nullptr, size, PROT_READ, MAP_SHARED, validName, 0, &address),
              MXM_ERR_UBSE_NOT_SUPPORTED);
    EXPECT_EQ(address, nullptr);
}

TEST_F(RackMemShmTest, TestConvertToFileModeAllCases)
{
    EXPECT_EQ(convertToFileMode(ShmOwnStatus::WRITE), S_IWUSR);
    EXPECT_EQ(convertToFileMode(ShmOwnStatus::READ), S_IRUSR);
    EXPECT_EQ(convertToFileMode(ShmOwnStatus::READWRITE), S_IRUSR | S_IWUSR);
    EXPECT_EQ(convertToFileMode(ShmOwnStatus::UNACCESS), 0);
    EXPECT_EQ(convertToFileMode(static_cast<ShmOwnStatus>(999)), 0);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapNullLocalPtr)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_READ;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void **local_ptr = nullptr; // 故意设置为nullptr

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapInvalidProt)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_NONE;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void *local_ptr = nullptr;
    void **local_ptr_addr = &local_ptr;

    // 模拟GetOwnStatus返回无效状态
    MOCKER(GetOwnStatus).stubs().with(prot).will(returnValue(OWNBUFF));

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr_addr);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapParameterCheckFailure)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_READ;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void *local_ptr = nullptr;
    void **local_ptr_addr = &local_ptr;

    // 模拟参数检查失败
    MOCKER(RackMemShmMmapParameterCheck)
        .stubs()
        .with(any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<int>(MXM_ERR_PARAM_INVALID)));

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr_addr);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapIpcMapFailure)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_READ;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void *local_ptr = nullptr;
    void **local_ptr_addr = &local_ptr;

    // 模拟参数检查成功
    MOCKER(RackMemShmMmapParameterCheck)
        .stubs()
        .with(any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟IPC调用失败
    MOCKER(ShmIpcCommand::IpcCallShmMap)
        .stubs()
        .with(any(), any(), any(), any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_MALLOC_FAIL)));

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr_addr);
    EXPECT_EQ(result, MXM_ERR_MALLOC_FAIL);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapSizeMismatch)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_READ;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void *local_ptr = nullptr;
    void **local_ptr_addr = &local_ptr;

    // 模拟参数检查成功
    MOCKER(RackMemShmMmapParameterCheck)
        .stubs()
        .with(any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟IPC调用成功但返回大小不匹配
    MOCKER(ShmIpcCommand::IpcCallShmMap)
        .stubs()
        .with(any(), any(), any(), outBound(mapSize * 2), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟取消映射调用
    MOCKER(ShmIpcCommand::IpcCallShmUnMap).stubs().with(any()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr_addr);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapFileMapFailure)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_READ;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void *local_ptr = nullptr;
    void **local_ptr_addr = &local_ptr;

    // 模拟参数检查成功
    MOCKER(RackMemShmMmapParameterCheck)
        .stubs()
        .with(any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟IPC调用成功
    std::vector<uint64_t> mockMemIds;
    size_t mockUnitSize = 4096;
    MOCKER(ShmIpcCommand::IpcCallShmMap)
        .stubs()
        .with(any(), any(), outBound(mockMemIds), outBound(mapSize), outBound(mockUnitSize), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟文件映射失败
    MOCKER_CPP(&RackMemShm::GetPreAllocateAddress, int (*)(void *start, size_t length, int flags, void **result))
        .stubs()
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_MMAP_VA_FAILED)));

    // 模拟取消映射调用
    MOCKER(ShmIpcCommand::IpcCallShmUnMap).stubs().with(any()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr_addr);
    EXPECT_EQ(result, MXM_ERR_MMAP_VA_FAILED);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapMemIdMapFailure)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_READ;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void *local_ptr = nullptr;
    void **local_ptr_addr = &local_ptr;
    std::vector<uint64_t> memIds = {123, 456};
    size_t unitSize = 8;

    // 模拟参数检查成功
    MOCKER(RackMemShmMmapParameterCheck)
        .stubs()
        .with(any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟IPC调用成功
    MOCKER(ShmIpcCommand::IpcCallShmMap)
        .stubs()
        .with(any(), any(), outBound(memIds), outBound(mapSize), outBound(unitSize), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟内存ID映射失败
    MOCKER(RackMemShmMmapByMemID).stubs().will(returnValue(static_cast<uint32_t>(MXM_ERR_MALLOC_FAIL)));

    // 模拟错误处理函数
    MOCKER(UnMapWhenError).stubs().with(any(), any(), any(), any()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr_addr);
    EXPECT_EQ(result, MXM_ERR_MALLOC_FAIL);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapAddMetaDataFailure)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_READ;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void *local_ptr = nullptr;
    void **local_ptr_addr = &local_ptr;
    std::vector<uint64_t> memIds = {123, 456};
    size_t unitSize = 8;

    // 模拟参数检查成功
    MOCKER(RackMemShmMmapParameterCheck)
        .stubs()
        .with(any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟IPC调用成功
    MOCKER(ShmIpcCommand::IpcCallShmMap)
        .stubs()
        .with(any(), any(), outBound(memIds), outBound(mapSize), outBound(unitSize), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟内存ID映射成功
    MOCKER(RackMemShmMmapByMemID).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟添加元数据失败
    MOCKER_CPP(&ShmMetaDataMgr::AddMetaData,
               uint32_t(*)(const std::string &name, void *mAddr, ShmAppMetaData &shmAppMetaData))
        .stubs()
        .with(any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_NULLPTR)));

    // 模拟错误处理函数
    MOCKER(UnMapWhenError).stubs().with(any(), any(), any(), any()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr_addr);
    EXPECT_EQ(result, MXM_ERR_NULLPTR);
}

TEST_F(RackMemShmTest, TestUbsMemShmMmapSuccess)
{
    // 定义测试变量
    void *start = nullptr;
    size_t mapSize = 4096;
    int prot = PROT_READ;
    int flags = MAP_SHARED;
    std::string name = "test_shm";
    off_t off = 0;
    void *local_ptr = nullptr;
    void **local_ptr_addr = &local_ptr;
    std::vector<uint64_t> memIds = {123, 456};
    size_t unitSize = 8;

    // 模拟参数检查成功
    MOCKER(RackMemShmMmapParameterCheck)
        .stubs()
        .with(any(), any(), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟IPC调用成功
    MOCKER(ShmIpcCommand::IpcCallShmMap)
        .stubs()
        .with(any(), any(), outBound(memIds), outBound(mapSize), outBound(unitSize), any(), any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟内存ID映射成功
    MOCKER(RackMemShmMmapByMemID).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    // 模拟添加元数据成功
    MOCKER_CPP(&ShmMetaDataMgr::AddMetaData,
               uint32_t(*)(const std::string &name, void *mAddr, ShmAppMetaData &shmAppMetaData))
        .stubs()
        .with(any(), any(), any())
        .will(returnValue(static_cast<uint32_t>(UBSM_OK)));

    int result = RackMemShm::GetInstance().UbsMemShmMmap(start, mapSize, prot, flags, name, off, local_ptr_addr);
    EXPECT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestUbsMemShmDeleteFailure)
{
    MOCKER(ShmIpcCommand::IpcCallShmDelete).stubs().will(returnValue(static_cast<uint32_t>(MXM_ERR_MALLOC_FAIL)));
    auto result = RackMemShm::GetInstance().UbsMemShmDelete("test");
    ASSERT_EQ(result, MXM_ERR_MALLOC_FAIL);
}

TEST_F(RackMemShmTest, TestUbsMemShmDeleteSuccess)
{
    MOCKER(ShmIpcCommand::IpcCallShmDelete).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    auto result = RackMemShm::GetInstance().UbsMemShmDelete("test");
    ASSERT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestCheckRegionParNumInvalidHigh)
{
    // 准备测试数据
    std::string baseNid = "node1";
    SHMRegionDesc region;
    region.num = MEM_TOPOLOGY_MAX_HOSTS + 1; // 超出最大值
    region.type = RackMemShmRegionType::ONE2ALL_SHARE;

    // 执行测试
    uint32_t result = RackMemShm::CheckRegionPar(baseNid, region);

    // 验证结果
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestCheckRegionParNumInvalidZero)
{
    SHMRegionDesc region;
    region.num = 0; // 无效数值
    region.type = RackMemShmRegionType::ALL2ALL_SHARE;

    uint32_t result = RackMemShm::CheckRegionPar("node1", region);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestCheckRegionParTypeInvalid)
{
    SHMRegionDesc region;
    region.num = 3;
    region.type = static_cast<RackMemShmRegionType>(999); // 999 为模拟的无效类型

    uint32_t result = RackMemShm::CheckRegionPar("node1", region);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestCheckRegionParBaseNidEmpty)
{
    SHMRegionDesc region;
    region.num = 2;
    region.type = RackMemShmRegionType::ALL2ALL_SHARE;

    // 空baseNid直接返回成功
    uint32_t result = RackMemShm::CheckRegionPar("", region);
    EXPECT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestCheckRegionParOne2AllNodeMismatch)
{
    std::string baseNid = "node1";
    SHMRegionDesc region;
    region.num = 1;
    region.type = RackMemShmRegionType::ONE2ALL_SHARE;
    strcpy_s(region.nodeId[0], 15, "different_node"); // 不匹配 baseNid

    std::cout << region.nodeId[0] << std::endl;
    uint32_t result = RackMemShm::CheckRegionPar(baseNid, region);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestCheckRegionParOne2AllValid)
{
    std::string baseNid = "node1";
    SHMRegionDesc region;
    region.num = 1;
    region.type = RackMemShmRegionType::ONE2ALL_SHARE;
    strcpy_s(region.nodeId[0], 6, baseNid.c_str());

    uint32_t result = RackMemShm::CheckRegionPar(baseNid, region);
    EXPECT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestCheckRegionParAll2AllMissingBase)
{
    std::string baseNid = "node_missing";
    SHMRegionDesc region;
    region.num = 3;
    region.type = RackMemShmRegionType::ALL2ALL_SHARE;
    strcpy_s(region.nodeId[0], 6, "node1");
    strcpy_s(region.nodeId[1], 6, "node2");
    strcpy_s(region.nodeId[2], 6, "node3");

    uint32_t result = RackMemShm::CheckRegionPar(baseNid, region);
    EXPECT_EQ(result, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestCheckRegionParAll2AllValid)
{
    std::string baseNid = "node2";
    SHMRegionDesc region;
    region.num = 3;
    region.type = RackMemShmRegionType::ALL2ALL_SHARE;
    strcpy_s(region.nodeId[0], 6, "node1");
    strcpy_s(region.nodeId[1], 6, "node2");
    strcpy_s(region.nodeId[2], 6, "node3");

    uint32_t result = RackMemShm::CheckRegionPar(baseNid, region);
    EXPECT_EQ(result, UBSM_OK);
}

TEST_F(RackMemShmTest, TestSetUbsMemShmSetOwnerShipInvalidParameter)
{
    std::string name = "test";
    void *start = nullptr;
    size_t length = 10;
    ShmOwnStatus status = READWRITE;
    auto ret = RackMemShm::GetInstance().UbsMemShmSetOwnerShip(name, start, length, status);
    ASSERT_EQ(ret, MXM_ERR_PARAM_INVALID);

    name = "";
    start = reinterpret_cast<void *>(0x123456789);
    length = 10;
    status = READWRITE;
    ret = RackMemShm::GetInstance().UbsMemShmSetOwnerShip(name, start, length, status);
    ASSERT_EQ(ret, MXM_ERR_PARAM_INVALID);

    name = "test";
    start = reinterpret_cast<void *>(0x123456789);
    length = 0;
    status = READWRITE;
    ret = RackMemShm::GetInstance().UbsMemShmSetOwnerShip(name, start, length, status);
    ASSERT_EQ(ret, MXM_ERR_PARAM_INVALID);
}

TEST_F(RackMemShmTest, TestUbsMemShmSetOwnerShipLookupFailure)
{
    std::string name = "test";
    void *start = reinterpret_cast<void *>(0x123456789);
    size_t length = 10;
    ShmOwnStatus status = READWRITE;
    MOCKER_CPP(&ShmMetaDataMgr::GetShmMetaFromName, int32_t(*)(const std::string &, ShmAppMetaData &))
        .stubs()
        .will(returnValue(static_cast<int32_t>(MXM_ERR_PARAM_INVALID)));

    auto ret = RackMemShm::GetInstance().UbsMemShmSetOwnerShip(name, start, length, status);
    ASSERT_EQ(ret, MXM_ERR_PARAM_INVALID);
}
} // namespace UT
