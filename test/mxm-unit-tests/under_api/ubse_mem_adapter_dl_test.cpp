/*
Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */
#include "gtest/gtest.h"
#include "mockcpp/mockcpp.hpp"
#include "numa_cpu_utils.h"
#include "region_manager.h"
#include "region_repository.h"
#include "securec.h"
#include "system_adapter.h"
#include "ubs_common_types.h"
#include "ubs_error.h"
#include "ubs_mem_monitor.h"
#include "ubse_mem_adapter.h"

extern "C" {
extern void *dlopen(const char *file, int mode);
extern int dlclose(void *handle);
extern void *dlsym(void *__restrict handle, const char *__restrict name);
}

using namespace ock::mxm;
using namespace ock::ubsm;
namespace UT {
class UbseMemAdapterDlTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

static constexpr size_t TEST_SHM_SIZE = 128UL * 1024UL * 1024UL;
static constexpr mode_t TEST_SHM_MODE = 0600;

TEST_F(UbseMemAdapterDlTest, UbseMemAdapterLoadLibrary_WhenUbseMemAdapterInitSuccess)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);
    UbseMemAdapter::Initialize();
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapterLoadLibrary_WhenDlopenFail)
{
    void *nullPtr = nullptr;
    MOCKER(SystemAdapter::DlOpen).stubs().will(returnValue(nullPtr));
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, ock::common::MXM_ERR_UBSE_LIB);
    GlobalMockObject::reset();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapterLoadLibrary_WhenDlsymFailed)
{
    int ubseFuncCount = 27;
    void *nullPtr = nullptr;
    void *notNullPtr = reinterpret_cast<void *>(1);
    for (int i = 0; i < ubseFuncCount; i++) {
        MOCKER(SystemAdapter::DlSym).expects(exactly(i + 1)).will(repeat(notNullPtr, i)).then(returnValue(nullPtr));
        DBG_LOGINFO("UbseMemAdapter::DlopenLibUbse ubseFuncCount." << i);
        EXPECT_EQ(UbseMemAdapter::Initialize(), ock::common::MXM_ERR_UBSE_LIB);
        GlobalMockObject::reset();
    }
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LookupRegionListSuccess)
{
    SHMRegions regions;
    auto ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    EXPECT_NE(regions.num, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_NumaLeaseMalloc)
{
    ock::mxm::LeaseMallocParam param;
    param.name = "UbseMemAdapter_NumaLeaseMalloc";
    param.regionName = "UbseMemAdapter_NumaLeaseMalloc_region";
    param.size = 128 * 1024 * 1024;
    param.isNuma = true;
    param.distance = ock::mxm::MEM_DISTANCE_L0;
    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;

    SHMRegions regions;
    auto ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    ock::share::service::RegionInfo regionInfo{param.regionName, 1, regions.region[0]};
    regionInfo.region.affinity[0] = true;
    ret = ock::share::service::RegionManager::GetInstance().CreateRegionInfo(regionInfo);
    EXPECT_EQ(ret, true);

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMalloc(param, result);
    DBG_LOGINFO("Malloc res is " << hr << ", name is " << param.name << ", numa is " << result.numaId << ", size is "
                                 << result.unitSize);
    EXPECT_EQ(hr, 0);
    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);
    ret = ock::share::service::RegionManager::GetInstance().DeleteRegionInfo(param.regionName);
    EXPECT_EQ(ret, true);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_FdLeaseMallocSuccess)
{
    ock::mxm::LeaseMallocParam param;
    param.name = "UbseMemAdapter_FdLeaseMallocSuccess";
    param.regionName = "default";
    param.size = 128 * 1024 * 1024;
    param.isNuma = false;
    param.distance = ock::mxm::MEM_DISTANCE_L0;
    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMalloc(param, result);
    DBG_LOGINFO("Malloc res is " << hr << ", name is " << param.name << ", numa is " << result.numaId << ", size is "
                                 << result.unitSize);
    EXPECT_EQ(hr, 0);

    mode_t mode = S_IRUSR | S_IWUSR;
    hr = ock::mxm::UbseMemAdapter::FdPermissionChange(param.name, param.appContext, mode);
    EXPECT_EQ(hr, 0);

    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_FdLeaseMallocWithLocSuccess)
{
    ock::mxm::LeaseMallocWithLocParam param;
    param.name = "UbseMemAdapter_FdLeaseMallocWithLocSuccess";
    param.regionName = "default";
    param.size = 128 * 1024 * 1024;
    param.isNuma = false;

    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;
    param.slotId = 1;
    param.socketId = 1;
    param.numaId = 0;
    param.portId = 1;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    DBG_LOGINFO("Malloc with loc res is " << hr << ", name is " << param.name << ", numa is " << result.numaId
                                          << ", size is " << result.unitSize);
    EXPECT_EQ(hr, 0);

    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);

    // error branch
    param.size = 1024;
    hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    param.size = 0; // time out
    hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, UBS_ERR_TIMED_OUT);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_NumaLeaseMallocWithLocSuccess)
{
    ock::mxm::LeaseMallocWithLocParam param;
    param.name = "UbseMemAdapter_NumaLeaseMallocWithLocSuccess";
    param.regionName = "default";
    param.size = 128 * 1024 * 1024;
    param.isNuma = true;

    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;
    param.slotId = 1;
    param.socketId = 1;
    param.numaId = 0;
    param.portId = 1;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    DBG_LOGINFO("Malloc with loc res is " << hr << ", name is " << param.name << ", numa is " << result.numaId
                                          << ", size is " << result.unitSize);
    EXPECT_EQ(hr, 0);

    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);

    // error branch
    param.size = 1024;
    hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    param.size = 0; // time out
    hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, UBS_ERR_TIMED_OUT);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmCreateSuccess)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "UbseMemAdapter_ShmCreateSuccess";
    createParam.size = 128 * 1024 * 1024;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = 0600;

    SHMRegions regions;
    auto ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    // 创建
    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importOwnerParam;
    importOwnerParam.name = "UbseMemAdapter_ShmCreateSuccess";
    importOwnerParam.length = 128 * 1024 * 1024;
    importOwnerParam.appContext.uid = 0;
    importOwnerParam.appContext.gid = 0;
    importOwnerParam.appContext.pid = 0;
    importOwnerParam.mode = 0600;

    // get
    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importOwnerParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    // mmap
    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach(importOwnerParam.name, ubsUserInfo, result);
    EXPECT_EQ(hr, 0);

    // unmap
    hr = ock::mxm::UbseMemAdapter::ShmDetach(importOwnerParam.name);
    EXPECT_EQ(hr, 0);

    // delete
    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, Rejects65ByteNamesBeforeBackendInitialization)
{
    MOCKER(UbseMemAdapter::Initialize).expects(never());
    const std::string name(65, 'a');
    CreateShmParam shm{};
    shm.name = name;
    EXPECT_EQ(UbseMemAdapter::ShmCreate(shm), MXM_ERR_PARAM_INVALID);
    CreateShmWithProviderParam provider("node2", 1, 1, UINT32_MAX);
    provider.name = name;
    EXPECT_EQ(UbseMemAdapter::ShmCreateWithProvider(provider), MXM_ERR_PARAM_INVALID);
    LeaseMallocResult result{};
    LeaseMallocParam lease{};
    lease.name = name;
    EXPECT_EQ(UbseMemAdapter::LeaseMalloc(lease, result), MXM_ERR_PARAM_INVALID);
    LeaseMallocWithLocParam locatedLease{};
    locatedLease.name = name;
    EXPECT_EQ(UbseMemAdapter::LeaseMallocWithLoc(locatedLease, result), MXM_ERR_PARAM_INVALID);
    GlobalMockObject::verify();
    GlobalMockObject::reset();
}

TEST_F(UbseMemAdapterDlTest, Preserves64ByteLeaseNames)
{
    LeaseMallocWithLocParam param{};
    param.name = std::string(64, 'l');
    param.regionName = "default";
    param.size = TEST_SHM_SIZE;
    param.slotId = 1;
    param.socketId = 1;
    param.numaId = 0;
    param.portId = 1;
    for (const bool isNuma : {false, true}) {
        param.isNuma = isNuma;
        LeaseMallocResult result{};
        ASSERT_EQ(UbseMemAdapter::LeaseMallocWithLoc(param, result), MXM_OK);
        EXPECT_FALSE(result.memIds.empty());
        EXPECT_EQ(UbseMemAdapter::LeaseFree(param.name, isNuma), MXM_OK);
    }
    UbseMemAdapter::Destroy();
}

static std::vector<std::pair<std::string, uint64_t>> rollbackCreatedMembers;
static std::vector<std::string> rollbackDeletedMembers;
static std::vector<DelayRemovedKey> scheduledPoolCleanup;
static uint32_t poolTopologyCount = 3;
static bool poolTopologyAliases = false;
static bool poolCreateFailure = true;
static std::vector<uint32_t> poolCreatedNodeIds;

static int32_t ListPoolTestNodes(ubs_topo_node_t **nodes, uint32_t *count)
{
    *count = poolTopologyCount;
    *nodes = static_cast<ubs_topo_node_t *>(calloc(*count, sizeof(ubs_topo_node_t)));
    if (*nodes == nullptr) {
        return UBS_ERR_NOT_SUPPORTED;
    }
    for (uint32_t index = 0; index < *count; ++index) {
        const auto name = "node" + std::to_string(index + 1);
        if (strcpy_s((*nodes)[index].host_name, sizeof((*nodes)[index].host_name), name.c_str()) != EOK) {
            free(*nodes);
            *nodes = nullptr;
            *count = 0;
            return UBS_ERR_NOT_SUPPORTED;
        }
        (*nodes)[index].slot_id = poolTopologyAliases ? 1 : index + 1;
    }
    return UBS_SUCCESS;
}

static int32_t FailThirdPoolCreate(const char *name, uint8_t *userInfo, uint64_t, const ubs_mem_nodes_t *,
                                   const ubs_mem_lender_t *lender)
{
    if (poolCreateFailure && rollbackCreatedMembers.size() == 2) {
        return UBS_ERR_NOT_SUPPORTED;
    }
    ubse_user_info_t info{};
    if (memcpy_s(&info, sizeof(info), userInfo, sizeof(info)) != EOK) {
        return UBS_ERR_NOT_SUPPORTED;
    }
    rollbackCreatedMembers.emplace_back(name, info.seqNo);
    poolCreatedNodeIds.emplace_back(lender->slot_id);
    return UBS_SUCCESS;
}

static int32_t FailPoolMemberRollback(const char *name)
{
    rollbackDeletedMembers.emplace_back(name);
    return rollbackDeletedMembers.size() == 1 ? UBS_ERR_TIMED_OUT : UBS_SUCCESS;
}

static void *PoolRollbackSymbols(void *handle, const char *name)
{
    if (strcmp(name, "ubs_topo_node_list") == 0) {
        return reinterpret_cast<void *>(ListPoolTestNodes);
    }
    if (strcmp(name, "ubs_mem_shm_create_with_lender") == 0) {
        return reinterpret_cast<void *>(FailThirdPoolCreate);
    }
    if (strcmp(name, "ubs_mem_shm_delete") == 0) {
        return reinterpret_cast<void *>(FailPoolMemberRollback);
    }
    return dlsym(handle, name);
}

static void CapturePoolCleanup(UBSMemMonitor *, const DelayRemovedKey &key)
{
    scheduledPoolCleanup.push_back(key);
}

TEST_F(UbseMemAdapterDlTest, MultiNodeRollbackFailureSchedulesMatchingCreateGeneration)
{
    UbseMemAdapter::Destroy();
    rollbackCreatedMembers.clear();
    rollbackDeletedMembers.clear();
    scheduledPoolCleanup.clear();
    poolTopologyCount = 3;
    poolTopologyAliases = false;
    MOCKER(SystemAdapter::DlSym).stubs().will(invoke(PoolRollbackSymbols));
    MOCKCPP_NS::mockAPI("AddDelayRemoveRecord", reinterpret_cast<void (*)(UBSMemMonitor *, const DelayRemovedKey &)>(
                                                    &UBSMemMonitor::AddDelayRemoveRecord))
        .expects(once())
        .will(invoke(CapturePoolCleanup));
    MultiNodeShmCreateParam param{};
    param.name = "rollback_generation";
    param.size = 12UL * 1024 * 1024;
    param.mode = 0600;
    param.hostNames = {"node1", "node2", "node3"};
    param.appContext = AppContext{100, 200, 300};
    EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param), MXM_ERR_UBSE_NOT_SUPPORTED);
    ASSERT_EQ(rollbackCreatedMembers.size(), 2);
    EXPECT_EQ(rollbackDeletedMembers, (std::vector<std::string>{"rollback_generation0201", "rollback_generation"}));
    ASSERT_EQ(scheduledPoolCleanup.size(), 1);
    EXPECT_EQ(scheduledPoolCleanup[0].name, rollbackCreatedMembers[1].first);
    EXPECT_EQ(scheduledPoolCleanup[0].createSeqNo, rollbackCreatedMembers[1].second);
    EXPECT_FALSE(scheduledPoolCleanup[0].isAttach);
    EXPECT_TRUE(scheduledPoolCleanup[0].timeOutScene);
    EXPECT_EQ(scheduledPoolCleanup[0].appCtx.pid, param.appContext.pid);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, MultiNodeNameBoundaryPreservesFourByteSuffix)
{
    UbseMemAdapter::Destroy();
    rollbackCreatedMembers.clear();
    poolTopologyCount = 2;
    poolTopologyAliases = false;
    MOCKER(SystemAdapter::DlSym).stubs().will(invoke(PoolRollbackSymbols));
    MultiNodeShmCreateParam param{};
    param.name = std::string(MAX_SHM_NAME_LENGTH - 4, 'a');
    param.size = 8UL * 1024 * 1024;
    param.mode = 0600;
    param.hostNames = {"node1", "node2"};
    param.appContext = AppContext{100, 200, 300};
    EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param), MXM_OK);
    ASSERT_EQ(rollbackCreatedMembers.size(), 2);
    EXPECT_EQ(rollbackCreatedMembers[0].first, param.name);
    EXPECT_EQ(rollbackCreatedMembers[1].first, param.name + "0101");
    EXPECT_EQ(rollbackCreatedMembers[1].first.size(), MAX_SHM_NAME_LENGTH);
    param.name.push_back('a');
    EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param), MXM_ERR_PARAM_INVALID);
    EXPECT_EQ(rollbackCreatedMembers.size(), 2);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, MultiNodeRejectsDuplicateHostsAndResolvedNodeIds)
{
    for (const bool aliases : {false, true}) {
        UbseMemAdapter::Destroy();
        poolTopologyCount = 2;
        poolTopologyAliases = aliases;
        rollbackCreatedMembers.clear();
        MOCKER(SystemAdapter::DlSym).stubs().will(invoke(PoolRollbackSymbols));
        MultiNodeShmCreateParam param{};
        param.name = "duplicate_pool";
        param.size = 8UL * 1024 * 1024;
        param.hostNames = aliases ? std::vector<std::string>{"node1", "node2"} :
                                    std::vector<std::string>{"node1", "node1"};
        EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param), MXM_ERR_PARAM_INVALID);
        EXPECT_TRUE(rollbackCreatedMembers.empty());
        UbseMemAdapter::Destroy();
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
    poolTopologyAliases = false;
}

TEST_F(UbseMemAdapterDlTest, MultiNodeAcceptsMoreThan16ProvidersThrough256)
{
    for (const uint32_t count : {17u, 256u, 257u}) {
        UbseMemAdapter::Destroy();
        poolTopologyCount = count;
        poolTopologyAliases = false;
        poolCreateFailure = false;
        rollbackCreatedMembers.clear();
        poolCreatedNodeIds.clear();
        rollbackDeletedMembers.clear();
        MOCKER(SystemAdapter::DlSym).stubs().will(invoke(PoolRollbackSymbols));
        MultiNodeShmCreateParam param{};
        param.name = "large_pool";
        param.size = count * 4UL * 1024 * 1024;
        for (uint32_t index = 0; index < count; ++index) {
            param.hostNames.emplace_back("node" + std::to_string(index + 1));
        }
        EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param),
                  count > MAX_MULTINODE_HOST_NUM ? MXM_ERR_PARAM_INVALID : MXM_OK);
        EXPECT_EQ(rollbackCreatedMembers.size(), count > MAX_MULTINODE_HOST_NUM ? 0UL : count);
        if (count <= MAX_MULTINODE_HOST_NUM) {
            EXPECT_EQ(rollbackCreatedMembers.front().first, param.name);
            EXPECT_EQ(rollbackCreatedMembers.back().first, count == 256 ? "large_poolffff" : "large_pool1010");
            EXPECT_EQ(poolCreatedNodeIds.size(), count);
            for (uint32_t index = 0; index < count; ++index) {
                EXPECT_EQ(poolCreatedNodeIds[index], index + 1);
            }
        }
        UbseMemAdapter::Destroy();
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
    poolCreateFailure = true;
}

TEST_F(UbseMemAdapterDlTest, MultiNodeServerRejectsLockMode)
{
    MultiNodeShmCreateParam param{};
    param.name = "lock_pool";
    param.size = 8UL * 1024 * 1024;
    param.hostNames = {"node1", "node2"};
    param.flags = UBSM_FLAG_WITH_LOCK;
    EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param), MXM_ERR_PARAM_INVALID);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, MultiNodeRejectsUnspecifiedPlaneBeforeBackendInitialization)
{
    MultiNodeShmCreateParam param{};
    param.name = "plane_pool";
    param.size = 8UL * 1024 * 1024;
    param.hostNames = {"node1", "node2"};
    param.socketId = UINT32_MAX;
    MOCKER(UbseMemAdapter::Initialize).expects(never());
    EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param), MXM_ERR_PARAM_INVALID);
    GlobalMockObject::verify();
}

TEST_F(UbseMemAdapterDlTest, MultiNodeRejectsEmptyProvidersBeforeBackendInitialization)
{
    MultiNodeShmCreateParam param{};
    param.name = "empty_pool";
    param.size = 8UL * 1024 * 1024;
    param.socketId = 0;
    MOCKER(UbseMemAdapter::Initialize).expects(never());
    EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param), MXM_ERR_PARAM_INVALID);
}

TEST_F(UbseMemAdapterDlTest, MultiNodeRejectsInvalidEvenDistributionBeforeBackendInitialization)
{
    MultiNodeShmCreateParam param{};
    param.name = "size_pool";
    param.socketId = 0;
    param.hostNames = {"node1", "node2"};
    MOCKER(UbseMemAdapter::Initialize).expects(never());
    for (const uint64_t size : {0UL, 1UL, 4UL * 1024 * 1024}) {
        param.size = size;
        EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(param), MXM_ERR_PARAM_INVALID);
    }
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_DeleteMultiNodeShmPoolSuccess)
{
    CreateShmParam createParam;
    createParam.size = TEST_SHM_SIZE;
    createParam.appContext = AppContext{100, 200, 300};
    createParam.flag = UBSM_FLAG_CACHE | UBSM_INNER_FLAG_MULTINODE;
    createParam.mode = TEST_SHM_MODE;

    SHMRegions regions;
    ASSERT_EQ(UbseMemAdapter::LookupRegionList(regions), MXM_OK);
    createParam.desc = regions.region[0];
    createParam.name = "multi_delete";
    ASSERT_EQ(UbseMemAdapter::ShmCreate(createParam), MXM_OK);
    createParam.name = "multi_delete0101";
    createParam.size = TEST_SHM_SIZE * 2;
    ASSERT_EQ(UbseMemAdapter::ShmCreate(createParam), MXM_OK);

    std::vector<std::string> names;
    ASSERT_EQ(UbseMemAdapter::GetMultiNodeShmNames("multi_delete", names, true), MXM_OK);
    EXPECT_EQ(names, (std::vector<std::string>{"multi_delete", "multi_delete0101"}));
    std::vector<MultiNodeShmMember> members;
    ASSERT_EQ(UbseMemAdapter::GetMultiNodeShmMembers("multi_delete", members, true), MXM_OK);
    ASSERT_EQ(members.size(), 2);
    EXPECT_EQ(members[0].name, "multi_delete");
    EXPECT_EQ(members[0].size, TEST_SHM_SIZE);
    EXPECT_EQ(members[1].name, "multi_delete0101");
    EXPECT_EQ(members[1].size, TEST_SHM_SIZE * 2);
    EXPECT_EQ(members[0].userInfo.udsInfo.uid, createParam.appContext.uid);
    EXPECT_EQ(members[1].userInfo.flag, createParam.flag);
    auto nonOwner = createParam.appContext;
    ++nonOwner.uid;
    EXPECT_EQ(UbseMemAdapter::DeleteMultiNodeShmPool("multi_delete", nonOwner), MXM_ERR_SHM_PERMISSION_DENIED);
    EXPECT_EQ(UbseMemAdapter::GetMultiNodeShmMembers("multi_delete", members, true), MXM_OK);
    EXPECT_EQ(members.size(), 2);
    const auto ret = UbseMemAdapter::DeleteMultiNodeShmPool("multi_delete", createParam.appContext);
    if (ret != MXM_OK) {
        UbseMemAdapter::ShmDelete("multi_delete", createParam.appContext);
        UbseMemAdapter::ShmDelete("multi_delete0101", createParam.appContext);
    }
    EXPECT_EQ(ret, MXM_OK);
    ubse_user_info_t userInfo{};
    EXPECT_EQ(UbseMemAdapter::ShmGetUserData("multi_delete", userInfo), MXM_ERR_SHM_NOT_EXIST);
    EXPECT_EQ(UbseMemAdapter::ShmGetUserData("multi_delete0101", userInfo), MXM_ERR_SHM_NOT_EXIST);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, RejectsNamesExceeding64ByteCapacity)
{
    CreateShmParam param{};
    MultiNodeShmCreateParam pool{};
    param.name = std::string(65, 'a');
    EXPECT_EQ(UbseMemAdapter::ShmCreate(param), MXM_ERR_PARAM_INVALID);
    pool.name = std::string(61, 'a');
    EXPECT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(pool), MXM_ERR_PARAM_INVALID);
}

TEST_F(UbseMemAdapterDlTest, PreservesLongSharedMemoryNames)
{
    CreateShmParam param{};
    param.size = TEST_SHM_SIZE;
    param.mode = TEST_SHM_MODE;
    SHMRegions regions{};
    ASSERT_EQ(UbseMemAdapter::LookupRegionList(regions), MXM_OK);
    param.desc = regions.region[0];
    for (const size_t length : {47UL, 48UL, 63UL, 64UL}) {
        param.name = std::string(length, 'a');
        ASSERT_EQ(UbseMemAdapter::ShmCreate(param), MXM_OK);
        ubse_user_info_t userInfo{};
        EXPECT_EQ(UbseMemAdapter::ShmGetUserData(param.name, userInfo), MXM_OK);
        ubsmem_shmem_info_t info{};
        EXPECT_EQ(UbseMemAdapter::ShmLookup(param.name, info), MXM_OK);
        EXPECT_STREQ(info.name, param.name.c_str());
        std::vector<ubsmem_shmem_desc_t> listed;
        EXPECT_EQ(UbseMemAdapter::ShmListLookup(param.name, listed), MXM_OK);
        ASSERT_EQ(listed.size(), 1);
        EXPECT_STREQ(listed[0].name, param.name.c_str());
        EXPECT_EQ(UbseMemAdapter::ShmDelete(param.name, param.appContext), MXM_OK);
    }
    MultiNodeShmCreateParam pool{};
    pool.size = 8UL * 1024 * 1024;
    pool.mode = TEST_SHM_MODE;
    pool.hostNames = {"node1", "node2"};
    for (const size_t length : {43UL, 44UL, 59UL, 60UL}) {
        pool.name = std::string(length, 'p');
        ASSERT_EQ(UbseMemAdapter::CreateMultiNodeShmPool(pool), MXM_OK);
        std::vector<MultiNodeShmMember> members;
        ASSERT_EQ(UbseMemAdapter::GetMultiNodeShmMembers(pool.name, members, true), MXM_OK);
        ASSERT_EQ(members.size(), 2);
        EXPECT_EQ(members[1].name, pool.name + "0101");
        EXPECT_EQ(UbseMemAdapter::DeleteMultiNodeShmPool(pool.name, pool.appContext), MXM_OK);
    }
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_IncompleteMultiNodePool)
{
    CreateShmParam param{};
    param.size = TEST_SHM_SIZE;
    param.appContext = AppContext{100, 200, 300};
    param.flag = UBSM_FLAG_CACHE | UBSM_INNER_FLAG_MULTINODE;
    param.mode = TEST_SHM_MODE;
    SHMRegions regions;
    ASSERT_EQ(UbseMemAdapter::LookupRegionList(regions), MXM_OK);
    param.desc = regions.region[0];
    param.name = "multi_incomplete";
    ASSERT_EQ(UbseMemAdapter::ShmCreate(param), MXM_OK);
    std::vector<std::string> names;
    EXPECT_EQ(UbseMemAdapter::GetMultiNodeShmNames(param.name, names, true), MXM_ERR_SHM_POOL_INCOMPLETE);
    EXPECT_TRUE(names.empty());
    EXPECT_EQ(UbseMemAdapter::GetMultiNodeShmNames(param.name, names), MXM_OK);
    EXPECT_EQ(names, (std::vector<std::string>{param.name}));
    EXPECT_EQ(UbseMemAdapter::DeleteMultiNodeShmPool(param.name, param.appContext), MXM_OK);
    param.name = "multi_incomplete0202";
    ASSERT_EQ(UbseMemAdapter::ShmCreate(param), MXM_OK);
    names.clear();
    EXPECT_EQ(UbseMemAdapter::GetMultiNodeShmNames("multi_incomplete", names, true), MXM_ERR_SHM_POOL_INCOMPLETE);
    EXPECT_EQ(UbseMemAdapter::DeleteMultiNodeShmPool("multi_incomplete", param.appContext), MXM_OK);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmCreateWithAffinity)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "UbseMemAdapter_ShmCreateWithAffinity";
    createParam.size = 128 * 1024 * 1024;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = 0600;

    SHMRegions regions;
    auto ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    MOCKER(NumaCpuUtils::IsAppBoundToOneNumaNode).stubs().will(returnValue(0));

    // 创建
    createParam.size = 1024; // 构造 size 小于4MB 失败
    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    createParam.size = 0; // 构造 TIME_OUT 失败
    hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    createParam.size = 128 * 1024 * 1024; // 正常创建
    hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importOwnerParam;
    importOwnerParam.name = "UbseMemAdapter_ShmCreateWithAffinity";
    importOwnerParam.length = 128 * 1024 * 1024;
    importOwnerParam.appContext.uid = 0;
    importOwnerParam.appContext.gid = 0;
    importOwnerParam.appContext.pid = 0;
    importOwnerParam.mode = 0600;

    // get
    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importOwnerParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    // mmap
    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach(importOwnerParam.name, ubsUserInfo, result);
    EXPECT_EQ(hr, 0);

    // unmap
    hr = ock::mxm::UbseMemAdapter::ShmDetach(importOwnerParam.name);
    EXPECT_EQ(hr, 0);

    // delete
    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LookUpClusterStatistic)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);
    ock::ubsm::ubsmemClusterInfo clusterInfo;
    auto hr = ock::mxm::UbseMemAdapter::LookUpClusterStatistic(clusterInfo);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmAttach)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "UbseMemAdapter_ShmAttach";
    createParam.size = 128 * 1024 * 1024;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = 0600;

    SHMRegions regions;
    auto ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    // 创建
    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importOwnerParam;
    importOwnerParam.name = createParam.name;
    importOwnerParam.length = 128 * 1024 * 1024;
    importOwnerParam.appContext.uid = 0;
    importOwnerParam.appContext.gid = 0;
    importOwnerParam.appContext.pid = 0;
    importOwnerParam.mode = 0600;

    // get
    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importOwnerParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    // attach
    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach(createParam.name, ubsUserInfo, result);
    EXPECT_EQ(hr, 0);

    ubsmem_shmem_info_t shmInfo;
    hr = ock::mxm::UbseMemAdapter::ShmLookup(createParam.name, shmInfo);
    EXPECT_EQ(hr, 0);

    std::vector<ubsmem_shmem_desc_t> nameList;
    hr = ock::mxm::UbseMemAdapter::ShmListLookup(createParam.name, nameList);
    EXPECT_EQ(hr, 0);

    // detach
    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDetach(importOwnerParam.name);
    EXPECT_EQ(hr, 0);

    // delete
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmCreateWithProvider)
{
    ock::mxm::CreateShmWithProviderParam createParam("node2", 1, 1, UINT32_MAX);
    createParam.name = "UbseMemAdapter_ShmCreateWithProvider";
    createParam.size = 128 * 1024 * 1024;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = 0600;

    // 创建
    createParam.size = 1024; // 构造 size 小于4MB 失败
    auto hr = ock::mxm::UbseMemAdapter::ShmCreateWithProvider(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    createParam.size = 0; // 构造 TIME_OUT 失败
    hr = ock::mxm::UbseMemAdapter::ShmCreateWithProvider(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    createParam.size = 128 * 1024 * 1024; // 正常创建
    hr = ock::mxm::UbseMemAdapter::ShmCreateWithProvider(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importOwnerParam;
    importOwnerParam.name = "UbseMemAdapter_ShmCreateWithProvider";
    importOwnerParam.length = 128 * 1024 * 1024;
    importOwnerParam.appContext.uid = 0;
    importOwnerParam.appContext.gid = 0;
    importOwnerParam.appContext.pid = 0;
    importOwnerParam.mode = 0600;

    // get
    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importOwnerParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    // mmap
    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach(importOwnerParam.name, ubsUserInfo, result);
    EXPECT_EQ(hr, 0);

    // unmap
    hr = ock::mxm::UbseMemAdapter::ShmDetach(importOwnerParam.name);
    EXPECT_EQ(hr, 0);

    // delete
    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}
TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LeaseMalloc_NumaCreateNotSupported)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);

    SHMRegions regions;
    ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);

    std::string regionName = "LeaseMalloc_NumaNotSupported_region";
    ock::share::service::RegionInfo regionInfo{regionName, 1, regions.region[0]};
    regionInfo.region.affinity[0] = true;
    ret = ock::share::service::RegionManager::GetInstance().CreateRegionInfo(regionInfo);
    EXPECT_EQ(ret, true);

    ock::mxm::LeaseMallocParam param;
    param.name = "LeaseMalloc_NumaNotSupported_NOT_SUPPORTED";
    param.regionName = regionName;
    param.size = TEST_SHM_SIZE;
    param.isNuma = true;
    param.distance = ock::mxm::MEM_DISTANCE_L0;
    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMalloc(param, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);

    ret = ock::share::service::RegionManager::GetInstance().DeleteRegionInfo(regionName);
    EXPECT_EQ(ret, true);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LeaseMalloc_FdCreateNotSupported)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);

    SHMRegions regions;
    ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);

    std::string regionName = "LeaseMalloc_FdNotSupported_region";
    ock::share::service::RegionInfo regionInfo{regionName, 1, regions.region[0]};
    regionInfo.region.affinity[0] = true;
    ret = ock::share::service::RegionManager::GetInstance().CreateRegionInfo(regionInfo);
    EXPECT_EQ(ret, true);

    ock::mxm::LeaseMallocParam param;
    param.name = "LeaseMalloc_FdNotSupported_NOT_SUPPORTED";
    param.regionName = regionName;
    param.size = TEST_SHM_SIZE;
    param.isNuma = false;
    param.distance = ock::mxm::MEM_DISTANCE_L0;
    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMalloc(param, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);

    ret = ock::share::service::RegionManager::GetInstance().DeleteRegionInfo(regionName);
    EXPECT_EQ(ret, true);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LeaseMallocWithLoc_NumaNotSupported)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);

    ock::mxm::LeaseMallocWithLocParam param;
    param.name = "LeaseLocNuma_NOT_SUPPORTED";
    param.regionName = "default";
    param.size = TEST_SHM_SIZE;
    param.isNuma = true;

    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;
    param.slotId = 1;
    param.socketId = 1;
    param.numaId = 0;
    param.portId = 1;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LeaseMallocWithLoc_FdNotSupported)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);

    ock::mxm::LeaseMallocWithLocParam param;
    param.name = "LeaseMallocWithLoc_FdNotSupported_NOT_SUPPORTED";
    param.regionName = "default";
    param.size = TEST_SHM_SIZE;
    param.isNuma = false;

    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;
    param.slotId = 1;
    param.socketId = 1;
    param.numaId = 0;
    param.portId = 1;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmCreateNotSupported)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "ShmCreateNotSupported_NOT_SUPPORTED";
    createParam.size = TEST_SHM_SIZE;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = TEST_SHM_MODE;

    SHMRegions regions;
    auto ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmCreateWithProviderNotSupported)
{
    ock::mxm::CreateShmWithProviderParam createParam("node2", 1, 1, UINT32_MAX);
    createParam.name = "ShmCreateWithProviderNotSupported_NOT_SUPPORTED";
    createParam.size = TEST_SHM_SIZE;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = TEST_SHM_MODE;

    auto hr = ock::mxm::UbseMemAdapter::ShmCreateWithProvider(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmDeleteNotSupported)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "UbseMemAdapter_ShmDeleteNotSupported_Normal";
    createParam.size = TEST_SHM_SIZE;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = TEST_SHM_MODE;

    SHMRegions regions;
    auto ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, 0);

    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;

    hr = ock::mxm::UbseMemAdapter::ShmDelete("ShmDeleteNotSupported_NOT_SUPPORTED", appContext);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);

    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmAttachNotSupported)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "UbseMemAdapter_ShmAttachNotSupported_Normal";
    createParam.size = TEST_SHM_SIZE;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = TEST_SHM_MODE;

    SHMRegions regions;
    auto ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importParam;
    importParam.name = createParam.name;
    importParam.length = TEST_SHM_SIZE;
    importParam.appContext.uid = 0;
    importParam.appContext.gid = 0;
    importParam.appContext.pid = 0;
    importParam.mode = TEST_SHM_MODE;

    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach("ShmAttachNotSupported_NOT_SUPPORTED", ubsUserInfo, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);

    hr = ock::mxm::UbseMemAdapter::ShmAttach(createParam.name, ubsUserInfo, result);
    EXPECT_EQ(hr, 0);
    hr = ock::mxm::UbseMemAdapter::ShmDetach(createParam.name);
    EXPECT_EQ(hr, 0);

    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LeaseFree_FdDeleteNotSupported)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);

    SHMRegions regions;
    ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);

    std::string regionName = "LeaseFree_FdNotSupported_region";
    ock::share::service::RegionInfo regionInfo{regionName, 1, regions.region[0]};
    regionInfo.region.affinity[0] = true;
    ret = ock::share::service::RegionManager::GetInstance().CreateRegionInfo(regionInfo);
    EXPECT_EQ(ret, true);

    ock::mxm::LeaseMallocParam param;
    param.name = "UbseMemAdapter_LeaseFree_FdNotSupported_Normal";
    param.regionName = regionName;
    param.size = TEST_SHM_SIZE;
    param.isNuma = false;
    param.distance = ock::mxm::MEM_DISTANCE_L0;
    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;

    ock::ubsm::LeaseMallocResult mallocResult{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMalloc(param, mallocResult);
    EXPECT_EQ(hr, 0);

    hr = ock::mxm::UbseMemAdapter::LeaseFree("LeaseFree_FdNotSupported_NOT_SUPPORTED", false);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);

    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);

    ret = ock::share::service::RegionManager::GetInstance().DeleteRegionInfo(regionName);
    EXPECT_EQ(ret, true);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LeaseFree_NumaDeleteNotSupported)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);

    SHMRegions regions;
    ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);

    std::string regionName = "LeaseFree_NumaNotSupported_region";
    ock::share::service::RegionInfo regionInfo{regionName, 1, regions.region[0]};
    regionInfo.region.affinity[0] = true;
    ret = ock::share::service::RegionManager::GetInstance().CreateRegionInfo(regionInfo);
    EXPECT_EQ(ret, true);

    ock::mxm::LeaseMallocParam param;
    param.name = "LeaseFree_NumaNotSupported_Normal";
    param.regionName = regionName;
    param.size = TEST_SHM_SIZE;
    param.isNuma = true;
    param.distance = ock::mxm::MEM_DISTANCE_L0;
    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;

    ock::ubsm::LeaseMallocResult mallocResult{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMalloc(param, mallocResult);
    EXPECT_EQ(hr, 0);

    hr = ock::mxm::UbseMemAdapter::LeaseFree("LeaseFree_NumaNotSupported_NOT_SUPPORTED", true);
    EXPECT_EQ(hr, MXM_ERR_UBSE_NOT_SUPPORTED);

    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);

    ret = ock::share::service::RegionManager::GetInstance().DeleteRegionInfo(regionName);
    EXPECT_EQ(ret, true);
    UbseMemAdapter::Destroy();
}
} // namespace UT
