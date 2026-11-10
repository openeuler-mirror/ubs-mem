/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "shm_ipc_handler_test.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <atomic>
#include <chrono>
#include <future>
#include "mxm_shm/ipc_handler.h"
#include "record_store.h"
#include "region_manager.h"
#include "region_repository.h"
#include "rpc_server.h"
#include "shm_manager.h"
#include "ubs_mem_monitor.h"
#include "ubse_mem_adapter_stub.h"
#include "ubsm_lock.h"
#include "zen_discovery.h"

using namespace UT;
using namespace ock::share::service;

namespace ock::share::service {
// mockcpp instantiates equality constraints for return types, even for invoke callbacks.
inline bool operator==(const AttachedShareMemory &left, const AttachedShareMemory &right)
{
    return left.name == right.name && left.size == right.size && left.state == right.state && left.pids == right.pids &&
           left.result.memIds == right.result.memIds && left.result.unitSize == right.result.unitSize &&
           left.result.createFlags == right.result.createFlags && left.result.openFlag == right.result.openFlag &&
           left.result.ownerUserId == right.result.ownerUserId && left.result.ownerGroupId == right.result.ownerGroupId;
}
} // namespace ock::share::service

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeCreateRejectsUnspecifiedPlaneBeforeAdapter)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::CreateMultiNodeShmPool, int (*)(const ock::mxm::MultiNodeShmCreateParam &))
        .expects(never());
    MultiNodeShmCreateRequest request{"plane_pool", 8UL * 1024 * 1024, 0600, 0, UINT32_MAX, {"node1", "node2"}, {}};
    CommonResponse response;
    MxmComUdsInfo info{};
    EXPECT_EQ(MxmServerMsgHandle::ShmCreateMultiNodePool(&request, &response, info), MXM_ERR_PARAM_INVALID);
    EXPECT_EQ(response.errCode_, MXM_ERR_PARAM_INVALID);
    GlobalMockObject::verify();
}

static int GetMultiNodeMembersForMap(const std::string &name, std::vector<ock::mxm::MultiNodeShmMember> &members,
                                     bool complete)
{
    EXPECT_TRUE(complete);
    ock::mxm::ubse_user_info_t info{};
    info.udsInfo.uid = 2000;
    info.udsInfo.gid = 2000;
    info.mode = 0666;
    info.flag = UBSM_INNER_FLAG_MULTINODE;
    members = {{name, 4UL * 1024 * 1024, info}, {name + "0101", 4UL * 1024 * 1024, info}};
    return MXM_OK;
}

static int GetCustomMultiNodeMembers(const std::string &name, std::vector<ock::mxm::MultiNodeShmMember> &members,
                                     bool complete)
{
    const auto ret = GetMultiNodeMembersForMap(name, members, complete);
    members[1].size *= 2;
    return ret;
}

static int GetPrivateMultiNodeMembers(const std::string &name, std::vector<ock::mxm::MultiNodeShmMember> &members,
                                      bool complete)
{
    const auto ret = GetMultiNodeMembersForMap(name, members, complete);
    for (auto &member : members) {
        member.userInfo.mode = 0600;
    }
    return ret;
}

static int AttachMultiNodeMemberForTest(const std::string &name, const ock::mxm::ubse_user_info_t &info,
                                        ock::mxm::AttachShmResult &result)
{
    EXPECT_EQ(info.udsInfo.uid, 2000);
    EXPECT_EQ(info.udsInfo.gid, 2000);
    EXPECT_EQ(info.udsInfo.pid, 1234);
    result.shmSize = 4UL * 1024 * 1024;
    result.unitSize = 4UL * 1024 * 1024;
    result.memIds = {name == "multi_map" ? 1UL : 2UL};
    result.flag = info.flag;
    return MXM_OK;
}

static int AttachCustomMultiNodeMember(const std::string &name, const ock::mxm::ubse_user_info_t &info,
                                       ock::mxm::AttachShmResult &result)
{
    const auto ret = AttachMultiNodeMemberForTest(name, info, result);
    if (name != "multi_map") {
        result.shmSize *= 2;
    }
    return ret;
}

static void MockMultiNodeMapRecords()
{
    MOCKER_CPP(&SHMManager::GetShareMemoryInfo, int (*)(SHMManager *, const std::string &, AttachedShareMemory &))
        .stubs()
        .will(returnValue(static_cast<int>(MXM_ERR_SHM_NOT_FOUND)));
    MOCKER_CPP(&SHMManager::PrepareAddShareMemoryInfo, int (*)(const std::string &, size_t, pid_t))
        .stubs()
        .will(returnValue(static_cast<int>(MXM_OK)));
    MOCKER_CPP(&SHMManager::AddFullShareMemoryInfo,
               int (*)(const std::string &, pid_t, const ock::ubsm::ShareMemImportResult &))
        .stubs()
        .will(returnValue(static_cast<int>(MXM_OK)));
    MOCKER_CPP(&SHMManager::RemoveMemoryInfo, int (*)(const std::string &, bool))
        .stubs()
        .will(returnValue(static_cast<int>(MXM_OK)));
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData, int (*)(const std::string &, ock::mxm::ubse_user_info_t &))
        .expects(never());
    MOCKER(MxmServerMsgHandle::ShmMap).expects(never());
}

static void MockMultiNodeMapDiscovery()
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::GetMultiNodeShmMembers,
               int (*)(const std::string &, std::vector<ock::mxm::MultiNodeShmMember> &, bool))
        .expects(once())
        .will(invoke(GetMultiNodeMembersForMap));
    MockMultiNodeMapRecords();
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapSuccess)
{
    MockMultiNodeMapDiscovery();
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(exactly(2))
        .will(invoke(AttachMultiNodeMemberForTest));
    MultiNodeShmMapRequest request{"multi_map", 8UL * 1024 * 1024, PROT_READ | PROT_WRITE};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_OK);
    EXPECT_EQ(response.errCode_, MXM_OK);
    EXPECT_EQ(response.memIds_, (std::vector<std::vector<uint64_t>>{{1}, {2}}));
    EXPECT_EQ(response.groupSizes_, (std::vector<uint64_t>{4UL * 1024 * 1024, 4UL * 1024 * 1024}));
    EXPECT_EQ(response.createdSize_, 8UL * 1024 * 1024);
    EXPECT_EQ(response.unitSize_, 4UL * 1024 * 1024);
    EXPECT_EQ(response.oflag_, O_RDWR);
    GlobalMockObject::verify();
}

static bool firstProviderIsLocal = true;

static int AttachMixedCacheMultiNodeMember(const std::string &name, const ock::mxm::ubse_user_info_t &info,
                                           ock::mxm::AttachShmResult &result)
{
    const auto ret = AttachMultiNodeMemberForTest(name, info, result);
    if ((name == "multi_map") == firstProviderIsLocal) {
        result.flag &= ~UBSM_FLAG_ONLY_IMPORT_NONCACHE;
    }
    return ret;
}

static int GetMixedCacheMultiNodeMembers(const std::string &name, std::vector<ock::mxm::MultiNodeShmMember> &members,
                                         bool complete)
{
    const auto ret = GetMultiNodeMembersForMap(name, members, complete);
    for (auto &member : members) {
        member.userInfo.flag |= UBSM_FLAG_ONLY_IMPORT_NONCACHE;
    }
    return ret;
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapPreservesDifferentLocalAndRemoteCacheFlags)
{
    for (const bool localFirst : {true, false}) {
        firstProviderIsLocal = localFirst;
        MOCKER_CPP(&ock::mxm::UbseMemAdapter::GetMultiNodeShmMembers,
                   int (*)(const std::string &, std::vector<ock::mxm::MultiNodeShmMember> &, bool))
            .expects(once())
            .will(invoke(GetMixedCacheMultiNodeMembers));
        MockMultiNodeMapRecords();
        MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
                   int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
            .expects(exactly(2))
            .will(invoke(AttachMixedCacheMultiNodeMember));
        MultiNodeShmMapRequest request{"multi_map", 8UL * 1024 * 1024, PROT_READ};
        MultiNodeShmMapResponse response;
        MxmComUdsInfo info{1234, 1000, 1000};
        EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_OK);
        EXPECT_EQ(response.flag_, UBSM_INNER_FLAG_MULTINODE | UBSM_FLAG_ONLY_IMPORT_NONCACHE);
        const uint64_t originalFlag = UBSM_INNER_FLAG_MULTINODE | UBSM_FLAG_ONLY_IMPORT_NONCACHE;
        EXPECT_EQ(response.groupFlags_, (localFirst ? std::vector<uint64_t>{UBSM_INNER_FLAG_MULTINODE, originalFlag} :
                                                      std::vector<uint64_t>{originalFlag, UBSM_INNER_FLAG_MULTINODE}));
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}

static int GetInconsistentCreationFlagMembers(const std::string &name,
                                              std::vector<ock::mxm::MultiNodeShmMember> &members, bool complete)
{
    const auto ret = GetMultiNodeMembersForMap(name, members, complete);
    members.back().userInfo.flag |= UBSM_FLAG_ONLY_IMPORT_NONCACHE;
    return ret;
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapRejectsInconsistentOriginalCreationFlagsBeforeAttach)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::GetMultiNodeShmMembers,
               int (*)(const std::string &, std::vector<ock::mxm::MultiNodeShmMember> &, bool))
        .expects(once())
        .will(invoke(GetInconsistentCreationFlagMembers));
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(never());
    MultiNodeShmMapRequest request{"multi_map", 8UL * 1024 * 1024, PROT_READ};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
    EXPECT_EQ(response.errCode_, MXM_ERR_SHM_POOL_LAYOUT_CONFLICT);
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapReturnsCustomGroupSizes)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::GetMultiNodeShmMembers,
               int (*)(const std::string &, std::vector<ock::mxm::MultiNodeShmMember> &, bool))
        .expects(once())
        .will(invoke(GetCustomMultiNodeMembers));
    MockMultiNodeMapRecords();
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(exactly(2))
        .will(invoke(AttachCustomMultiNodeMember));
    MultiNodeShmMapRequest request{"multi_map", 12UL * 1024 * 1024, PROT_READ};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_OK);
    EXPECT_EQ(response.groupSizes_, (std::vector<uint64_t>{4UL * 1024 * 1024, 8UL * 1024 * 1024}));
    EXPECT_EQ(response.createdSize_, 12UL * 1024 * 1024);
    GlobalMockObject::verify();
}

static int AttachMultiNodeMemberWithSecondFailure(const std::string &name, const ock::mxm::ubse_user_info_t &info,
                                                  ock::mxm::AttachShmResult &result)
{
    return name == "multi_map" ? AttachMultiNodeMemberForTest(name, info, result) : MXM_ERR_UBSE_INNER;
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapRollsBackEarlierMembers)
{
    MockMultiNodeMapDiscovery();
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(exactly(2))
        .will(invoke(AttachMultiNodeMemberWithSecondFailure));
    MOCKER(MxmServerMsgHandle::RollbackMultiNodeShmMember).expects(once()).will(returnValue(static_cast<int>(MXM_OK)));
    MultiNodeShmMapRequest request{"multi_map", 8UL * 1024 * 1024, PROT_READ};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_ERR_UBSE_INNER);
    EXPECT_EQ(response.errCode_, MXM_ERR_UBSE_INNER);
    EXPECT_TRUE(response.memIds_.empty());
    EXPECT_TRUE(response.groupSizes_.empty());
    GlobalMockObject::verify();
}

static std::vector<ock::ubsm::DelayRemovedKey> delayedRollbackKeys;

static void CaptureDelayedRollback(ock::ubsm::UBSMemMonitor *, const ock::ubsm::DelayRemovedKey &key)
{
    delayedRollbackKeys.emplace_back(key);
}

static void CaptureRollbackTasks()
{
    delayedRollbackKeys.clear();
    MOCKER_CPP(&ock::ubsm::UBSMemMonitor::AddDelayRemoveRecord,
               void (*)(ock::ubsm::UBSMemMonitor *, const ock::ubsm::DelayRemovedKey &))
        .stubs()
        .will(invoke(CaptureDelayedRollback));
}

static int CreateRollbackRecord(const std::string &name, pid_t pid)
{
    auto &manager = SHMManager::GetInstance();
    auto ret = manager.PrepareAddShareMemoryInfo(name, 4UL * 1024 * 1024, pid);
    if (ret != MXM_OK) {
        return ret;
    }
    ock::ubsm::ShareMemImportResult result{};
    result.memIds = {123};
    result.unitSize = 4UL * 1024 * 1024;
    result.createFlags = UBSM_INNER_FLAG_MULTINODE;
    result.openFlag = O_RDWR;
    return manager.AddFullShareMemoryInfo(name, pid, result);
}

TEST_F(ShmIpcHandlerTestSuite, TestFailedRollbackQueuesReferenceAndRetryIsIdempotent)
{
    const std::string name = "rollback_retry";
    MxmComUdsInfo info{1234, 1000, 1000};
    ASSERT_EQ(CreateRollbackRecord(name, info.pid), MXM_OK);
    CaptureRollbackTasks();
    MOCKER(ock::mxm::UbseMemAdapter::ShmDetach)
        .expects(exactly(2))
        .will(returnValue(static_cast<int>(MXM_ERR_UBSE_INNER)))
        .then(returnValue(static_cast<int>(MXM_OK)));
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info), MXM_ERR_UBSE_INNER);
    ASSERT_EQ(delayedRollbackKeys.size(), 1);
    const auto key = delayedRollbackKeys.front();
    EXPECT_EQ(key.name, name);
    EXPECT_EQ(key.appCtx.pid, info.pid);
    EXPECT_EQ(key.appCtx.uid, info.uid);
    EXPECT_EQ(key.appCtx.gid, info.gid);
    EXPECT_NE(key.shmRollbackReferenceId, 0);
    AttachedShareMemory memory{};
    ASSERT_EQ(SHMManager::GetInstance().GetShareMemoryInfo(name, memory), MXM_OK);
    EXPECT_EQ(memory.state, ock::ubsm::RecordState::FINISH);
    EXPECT_EQ(memory.referenceIds.at(info.pid), key.shmRollbackReferenceId);
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info, key.shmRollbackReferenceId), MXM_OK);
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info, key.shmRollbackReferenceId), MXM_OK);
    EXPECT_EQ(SHMManager::GetInstance().GetShareMemoryInfo(name, memory), MXM_ERR_SHM_NOT_FOUND);
    EXPECT_EQ(delayedRollbackKeys.size(), 1);
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestDelayedRollbackPreservesOtherClientReference)
{
    const std::string name = "rollback_shared";
    MxmComUdsInfo info{1234, 1000, 1000};
    ASSERT_EQ(CreateRollbackRecord(name, info.pid), MXM_OK);
    CaptureRollbackTasks();
    MOCKER(ock::mxm::UbseMemAdapter::ShmDetach).expects(once()).will(returnValue(static_cast<int>(MXM_ERR_UBSE_INNER)));
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info), MXM_ERR_UBSE_INNER);
    ASSERT_EQ(delayedRollbackKeys.size(), 1);
    ASSERT_EQ(SHMManager::GetInstance().AddMemoryUserInfo(name, 5678), MXM_OK);
    const auto id = delayedRollbackKeys.front().shmRollbackReferenceId;
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info, id), MXM_OK);
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info, id), MXM_OK);
    AttachedShareMemory memory{};
    ASSERT_EQ(SHMManager::GetInstance().GetShareMemoryInfo(name, memory), MXM_OK);
    EXPECT_EQ(memory.pids, (std::unordered_set<pid_t>{5678}));
    EXPECT_EQ(memory.referenceIds.count(info.pid), 0);
    EXPECT_EQ(SHMManager::GetInstance().RemoveMemoryUserInfo(name, 5678), MXM_OK);
    EXPECT_EQ(SHMManager::GetInstance().RemoveMemoryInfo(name), MXM_OK);
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestOldRollbackDoesNotRemoveRecreatedOrRenewedReference)
{
    auto &manager = SHMManager::GetInstance();
    const std::string name = "rollback_generation";
    MxmComUdsInfo info{1234, 1000, 1000};
    ASSERT_EQ(CreateRollbackRecord(name, info.pid), MXM_OK);
    AttachedShareMemory memory{};
    ASSERT_EQ(manager.GetShareMemoryInfo(name, memory), MXM_OK);
    const auto oldId = memory.referenceIds.at(info.pid);
    MOCKER(ock::mxm::UbseMemAdapter::ShmDetach).expects(never());
    ASSERT_EQ(manager.AddMemoryUserInfo(name, 5678), MXM_OK);
    ASSERT_EQ(manager.RemoveMemoryUserInfo(name, info.pid), MXM_OK);
    ASSERT_EQ(manager.AddMemoryUserInfo(name, info.pid), MXM_OK);
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info, oldId), MXM_OK);
    ASSERT_EQ(manager.GetShareMemoryInfo(name, memory), MXM_OK);
    EXPECT_EQ(memory.pids.size(), 2);
    EXPECT_NE(memory.referenceIds.at(info.pid), oldId);
    ASSERT_EQ(manager.RemoveMemoryUserInfo(name, info.pid), MXM_OK);
    ASSERT_EQ(manager.RemoveMemoryUserInfo(name, 5678), MXM_OK);
    ASSERT_EQ(manager.RemoveMemoryInfo(name), MXM_OK);
    ASSERT_EQ(CreateRollbackRecord(name, info.pid), MXM_OK);
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info, oldId), MXM_OK);
    ASSERT_EQ(manager.GetShareMemoryInfo(name, memory), MXM_OK);
    EXPECT_EQ(memory.pids.count(info.pid), 1);
    EXPECT_NE(memory.referenceIds.at(info.pid), oldId);
    EXPECT_EQ(manager.RemoveMemoryUserInfo(name, info.pid), MXM_OK);
    EXPECT_EQ(manager.RemoveMemoryInfo(name), MXM_OK);
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestRollbackRecordRemovalRetryDoesNotDetachTwice)
{
    const std::string name = "rollback_record";
    MxmComUdsInfo info{1234, 1000, 1000};
    ASSERT_EQ(CreateRollbackRecord(name, info.pid), MXM_OK);
    CaptureRollbackTasks();
    MOCKER(ock::mxm::UbseMemAdapter::ShmDetach).expects(once()).will(returnValue(static_cast<int>(MXM_OK)));
    MOCKER_CPP(&ock::ubsm::RecordStore::DelShmImportRecord, int (*)(const std::string &))
        .expects(once())
        .will(returnValue(-1));
    EXPECT_NE(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info), MXM_OK);
    ASSERT_EQ(delayedRollbackKeys.size(), 1);
    const auto id = delayedRollbackKeys.front().shmRollbackReferenceId;
    AttachedShareMemory memory{};
    ASSERT_EQ(SHMManager::GetInstance().GetShareMemoryInfo(name, memory), MXM_OK);
    EXPECT_EQ(memory.state, ock::ubsm::RecordState::DELETED);
    EXPECT_TRUE(memory.pids.empty());
    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(ock::mxm::UbseMemAdapter::ShmDetach).expects(never());
    EXPECT_EQ(MxmServerMsgHandle::RollbackMultiNodeShmMember(name, info, id), MXM_OK);
    EXPECT_EQ(SHMManager::GetInstance().GetShareMemoryInfo(name, memory), MXM_ERR_SHM_NOT_FOUND);
    GlobalMockObject::verify();
}

static int AttachRollbackPoolMember(const std::string &name, const ock::mxm::ubse_user_info_t &info,
                                    ock::mxm::AttachShmResult &result)
{
    return name == "rollback_attach" ? AttachMultiNodeMemberForTest(name, info, result) : MXM_ERR_UBSE_INNER;
}

TEST_F(ShmIpcHandlerTestSuite, TestAttachFailureQueuesFailedDetachAndPreservesOriginalError)
{
    MOCKER(ock::mxm::UbseMemAdapter::GetMultiNodeShmMembers).stubs().will(invoke(GetMultiNodeMembersForMap));
    MOCKER(ock::mxm::UbseMemAdapter::ShmAttach).expects(exactly(2)).will(invoke(AttachRollbackPoolMember));
    MOCKER(ock::mxm::UbseMemAdapter::ShmDetach)
        .expects(once())
        .will(returnValue(static_cast<int>(MXM_ERR_UBSE_NOT_SUPPORTED)));
    CaptureRollbackTasks();
    MultiNodeShmMapRequest request{"rollback_attach", 8UL * 1024 * 1024, PROT_READ};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_ERR_UBSE_INNER);
    EXPECT_EQ(response.errCode_, MXM_ERR_UBSE_INNER);
    EXPECT_TRUE(response.memIds_.empty());
    ASSERT_EQ(delayedRollbackKeys.size(), 1);
    EXPECT_EQ(delayedRollbackKeys.front().name, request.name_);
    EXPECT_NE(delayedRollbackKeys.front().shmRollbackReferenceId, 0);
    auto &manager = SHMManager::GetInstance();
    EXPECT_EQ(manager.RemoveMemoryUserInfo(request.name_, info.pid), MXM_OK);
    EXPECT_EQ(manager.RemoveMemoryInfo(request.name_), MXM_OK);
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapRejectsOversizedRequest)
{
    MockMultiNodeMapDiscovery();
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(never());
    MultiNodeShmMapRequest request{"multi_map", 12UL * 1024 * 1024, PROT_READ};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_ERR_PARAM_INVALID);
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapRejectsIncompletePool)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::GetMultiNodeShmMembers,
               int (*)(const std::string &, std::vector<ock::mxm::MultiNodeShmMember> &, bool))
        .stubs()
        .will(returnValue(static_cast<int>(MXM_ERR_SHM_POOL_INCOMPLETE)));
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(never());
    MultiNodeShmMapRequest request{"multi_map", 8UL * 1024 * 1024, PROT_READ};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_ERR_SHM_POOL_INCOMPLETE);
    EXPECT_EQ(response.errCode_, MXM_ERR_SHM_POOL_INCOMPLETE);
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapChecksNonOwnerPermissions)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::GetMultiNodeShmMembers,
               int (*)(const std::string &, std::vector<ock::mxm::MultiNodeShmMember> &, bool))
        .stubs()
        .will(invoke(GetPrivateMultiNodeMembers));
    MockMultiNodeMapRecords();
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(never());
    MultiNodeShmMapRequest request{"multi_map", 8UL * 1024 * 1024, PROT_READ};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_ERR_SHM_PERMISSION_DENIED);
    GlobalMockObject::verify();
}

static int GetReadableMultiNodeMembers(const std::string &name, std::vector<ock::mxm::MultiNodeShmMember> &members,
                                       bool complete)
{
    const auto ret = GetMultiNodeMembersForMap(name, members, complete);
    for (auto &member : members) {
        member.userInfo.mode = 0644;
    }
    return ret;
}

static int GetCachedMultiNodeMember(SHMManager *, const std::string &name, AttachedShareMemory &memory)
{
    memory.name = name;
    memory.size = 4UL * 1024 * 1024;
    memory.state = ock::ubsm::RecordState::FINISH;
    memory.pids = {9999};
    memory.result.createFlags = UBSM_INNER_FLAG_MULTINODE;
    memory.result.unitSize = 4UL * 1024 * 1024;
    memory.result.memIds = {name == "multi_map" ? 1UL : 2UL};
    memory.result.openFlag = O_RDWR;
    return MXM_OK;
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeMapReusesCacheWithCallerPermissions)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::GetMultiNodeShmMembers,
               int (*)(const std::string &, std::vector<ock::mxm::MultiNodeShmMember> &, bool))
        .expects(once())
        .will(invoke(GetReadableMultiNodeMembers));
    MOCKER_CPP(&SHMManager::GetShareMemoryInfo, int (*)(SHMManager *, const std::string &, AttachedShareMemory &))
        .stubs()
        .will(invoke(GetCachedMultiNodeMember));
    MOCKER_CPP(&SHMManager::AddMemoryUserInfo, int (*)(const std::string &, pid_t))
        .expects(exactly(2))
        .will(returnValue(static_cast<int>(MXM_OK)));
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(never());
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData, int (*)(const std::string &, ock::mxm::ubse_user_info_t &))
        .expects(never());
    MultiNodeShmMapRequest request{"multi_map", 8UL * 1024 * 1024, PROT_READ};
    MultiNodeShmMapResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmMapMultiNodePool(&request, &response, info), MXM_OK);
    EXPECT_EQ(response.oflag_, O_RDONLY);
    EXPECT_EQ(response.memIds_, (std::vector<std::vector<uint64_t>>{{1}, {2}}));
    GlobalMockObject::verify();
}

static std::vector<AttachedShareMemory> multiNodeLocalMembers;

static std::vector<AttachedShareMemory> GetMultiNodeLocalMembers(SHMManager *, uint32_t)
{
    return multiNodeLocalMembers;
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeUnmapAlreadyReleasedIsIdempotent)
{
    multiNodeLocalMembers.clear();
    MOCKER_CPP(&SHMManager::GetMemoryUsersByPid, std::vector<AttachedShareMemory>(*)(SHMManager *, uint32_t))
        .stubs()
        .will(invoke(GetMultiNodeLocalMembers));
    MOCKER(MxmServerMsgHandle::ShmUnmap).expects(never());
    MultiNodeShmUnmapRequest request{"multi_map"};
    CommonResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmUnmapMultiNodePool(&request, &response, info), MXM_OK);
    EXPECT_EQ(response.errCode_, MXM_OK);
}

TEST_F(ShmIpcHandlerTestSuite, TestMultiNodeUnmapUsesLocalMembersAndContinuesAfterFailure)
{
    AttachedShareMemory base{};
    base.name = "multi_map";
    base.result.createFlags = UBSM_INNER_FLAG_MULTINODE;
    auto child = base;
    child.name = "multi_map0101";
    AttachedShareMemory ordinary{};
    ordinary.name = "multi_map0202";
    multiNodeLocalMembers = {base, child, ordinary};
    MOCKER_CPP(&SHMManager::GetMemoryUsersByPid, std::vector<AttachedShareMemory>(*)(SHMManager *, uint32_t))
        .stubs()
        .will(invoke(GetMultiNodeLocalMembers));
    MOCKER(MxmServerMsgHandle::ShmUnmap)
        .expects(exactly(2))
        .will(returnValue(static_cast<int>(MXM_ERR_UBSE_INNER)))
        .then(returnValue(static_cast<int>(MXM_OK)));
    MultiNodeShmUnmapRequest request{"multi_map"};
    CommonResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmUnmapMultiNodePool(&request, &response, info), MXM_ERR_UBSE_INNER);
    EXPECT_EQ(response.errCode_, MXM_ERR_UBSE_INNER);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmemNull)
{
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmMap(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmUnmap(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmDelete(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::RegionLookupRegionList(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::RegionCreateRegion(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::RegionLookupRegion(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::RegionDestroyRegion(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmWriteLock(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmReadLock(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmUnLock(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmQueryNode(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmQueryDlockStatus(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmAttach(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmDetach(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmListLookup(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmLookup(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::RpcQueryNodeInfo(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_NULLPTR);
    ret = MxmServerMsgHandle::ShmDeleteMultiNodePool(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_PARAM_INVALID);
    ret = MxmServerMsgHandle::ShmMapMultiNodePool(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_PARAM_INVALID);
    ret = MxmServerMsgHandle::ShmUnmapMultiNodePool(nullptr, nullptr, info);
    ASSERT_EQ(ret, MXM_ERR_PARAM_INVALID);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmDeleteMultiNodePoolSuccess)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::DeleteMultiNodeShmPool,
               int (*)(const std::string &name, const ock::ubsm::AppContext &appContext))
        .stubs()
        .will(returnValue(static_cast<int>(UBSM_OK)));
    auto request = std::make_shared<MultiNodeShmDeleteRequest>("multi_pool");
    auto response = std::make_shared<CommonResponse>();
    MxmComUdsInfo info{1234, 1000, 1000};

    EXPECT_EQ(MxmServerMsgHandle::ShmDeleteMultiNodePool(request.get(), response.get(), info), UBSM_OK);
    EXPECT_EQ(response->errCode_, UBSM_OK);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmDeleteMultiNodePoolFailure)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::DeleteMultiNodeShmPool,
               int (*)(const std::string &name, const ock::ubsm::AppContext &appContext))
        .stubs()
        .will(returnValue(static_cast<int>(MXM_ERR_SHM_PERMISSION_DENIED)));
    auto request = std::make_shared<MultiNodeShmDeleteRequest>("multi_pool");
    auto response = std::make_shared<CommonResponse>();
    MxmComUdsInfo info{1234, 1000, 1000};

    EXPECT_EQ(MxmServerMsgHandle::ShmDeleteMultiNodePool(request.get(), response.get(), info),
              MXM_ERR_SHM_PERMISSION_DENIED);
    EXPECT_EQ(response->errCode_, MXM_ERR_SHM_PERMISSION_DENIED);
}

static int GetUncachedMultiNodeMember(SHMManager *, const std::string &, AttachedShareMemory &memory)
{
    memory = AttachedShareMemory{};
    return MXM_ERR_SHM_NOT_FOUND;
}

static int GetMultiNodeMemberUserInfo(const std::string &, ock::mxm::ubse_user_info_t &info)
{
    info = ock::mxm::ubse_user_info_t{};
    info.flag = UBSM_INNER_FLAG_MULTINODE;
    return MXM_OK;
}

TEST_F(ShmIpcHandlerTestSuite, TestOrdinaryMapRejectsCachedMultiNodeMembers)
{
    MOCKER_CPP(&SHMManager::GetShareMemoryInfo, int (*)(SHMManager *, const std::string &, AttachedShareMemory &))
        .stubs()
        .will(invoke(GetCachedMultiNodeMember));
    MOCKER_CPP(&SHMManager::AddMemoryUserInfo, int (*)(const std::string &, pid_t)).expects(never());
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData, int (*)(const std::string &, ock::mxm::ubse_user_info_t &))
        .expects(never());
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(never());
    MxmComUdsInfo info{1234, 1000, 1000};
    for (const auto &name : {"multi_map", "multi_map0101"}) {
        ShmMapRequest request{"", name, 4UL * 1024 * 1024, 0600, PROT_READ};
        ShmMapResponse response;
        EXPECT_EQ(MxmServerMsgHandle::ShmMap(&request, &response, info), MXM_ERR_PARAM_INVALID);
        EXPECT_EQ(response.errCode_, MXM_ERR_PARAM_INVALID);
        EXPECT_TRUE(response.memIds_.empty());
    }
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestOrdinaryMapRejectsUncachedMultiNodeMembers)
{
    MOCKER_CPP(&SHMManager::GetShareMemoryInfo, int (*)(SHMManager *, const std::string &, AttachedShareMemory &))
        .stubs()
        .will(invoke(GetUncachedMultiNodeMember));
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData, int (*)(const std::string &, ock::mxm::ubse_user_info_t &))
        .expects(exactly(2))
        .will(invoke(GetMultiNodeMemberUserInfo));
    MOCKER_CPP(&SHMManager::PrepareAddShareMemoryInfo, int (*)(const std::string &, size_t, pid_t)).expects(never());
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmAttach,
               int (*)(const std::string &, const ock::mxm::ubse_user_info_t &, ock::mxm::AttachShmResult &))
        .expects(never());
    MxmComUdsInfo info{1234, 1000, 1000};
    for (const auto &name : {"multi_map", "multi_map0101"}) {
        ShmMapRequest request{"", name, 4UL * 1024 * 1024, 0600, PROT_READ};
        ShmMapResponse response;
        EXPECT_EQ(MxmServerMsgHandle::ShmMap(&request, &response, info), MXM_ERR_PARAM_INVALID);
        EXPECT_EQ(response.errCode_, MXM_ERR_PARAM_INVALID);
        EXPECT_TRUE(response.memIds_.empty());
    }
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestShmMapFail)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData,
               int (*)(const std::string &name, ock::mxm::ubse_user_info_t &ubsUserInfo))
        .stubs()
        .will(returnValue(0));

    auto request = std::make_shared<ShmMapRequest>("default", "", 1, 0, 1);
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmMapResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmMap(request.get(), response.get(), info);
    ASSERT_NE(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmMapFail01)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData,
               int (*)(const std::string &name, ock::mxm::ubse_user_info_t &ubsUserInfo))
        .stubs()
        .will(returnValue(-1));

    auto request = std::make_shared<ShmMapRequest>("default", "", 1, 0, 1);
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmMapResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmMap(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmMapHandleExistingShm)
{
    SHMManager::GetInstance().PrepareAddShareMemoryInfo("xxx", 134217728, 111);
    SHMManager::GetInstance().AddFullShareMemoryInfo("xxx", 111,
                                                     {.memIds = {1},
                                                      .unitSize = 134217728,
                                                      .createFlags = 0,
                                                      .openFlag = O_RDWR,
                                                      .ownerUserId = 0,
                                                      .ownerGroupId = 0});
    MOCKER_CPP(&SHMManager::AddMemoryUserInfo, int32_t(*)(const std::string &name, pid_t)).stubs().will(returnValue(0));

    auto request = std::make_shared<ShmMapRequest>("default", "xxx", 134217728, 0, 1);
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmMapResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmMap(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
    SHMManager::GetInstance().RemoveMemoryInfo("xxx");
}

TEST_F(ShmIpcHandlerTestSuite, TestShmUnmap)
{
    MOCKER_CPP(&SHMManager::GetShareMemoryInfo,
               int32_t(*)(SHMManager *, const std::string &name, AttachedShareMemory &memory))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&SHMManager::GetMemoryUsersCountByName, int32_t(*)(const std::string &name, size_t &count))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&SHMManager::GetShareMemoryInfo, int32_t(*)(const std::string &name, AttachedShareMemory &memory))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&SHMManager::RemoveMemoryUserInfo, int32_t(*)(const std::string &name, pid_t))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&SHMManager::RemoveMemoryInfo, int32_t(*)(const std::string &name)).stubs().will(returnValue(0));

    MOCKER_CPP(&SHMManager::UpdateShareMemoryRecordState,
               int32_t(*)(const std::string &name, ock::ubsm::RecordState state))
        .stubs()
        .will(returnValue(0));

    auto request = std::make_shared<ShmUnmapRequest>("default", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<CommonResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmUnmap(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmUnmapRetryDoesNotDetachAnotherProcess)
{
    MOCKER_CPP(&SHMManager::GetShareMemoryInfo, int (*)(SHMManager *, const std::string &, AttachedShareMemory &))
        .stubs()
        .will(invoke(GetCachedMultiNodeMember));
    MOCKER_CPP(&SHMManager::GetMemoryUsersCountByName, int (*)(const std::string &, size_t &)).expects(never());
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmDetach, int (*)(const std::string &)).expects(never());
    ShmUnmapRequest request{"", "multi_map"};
    CommonResponse response;
    MxmComUdsInfo info{1234, 1000, 1000};
    EXPECT_EQ(MxmServerMsgHandle::ShmUnmap(&request, &response, info), MXM_OK);
    EXPECT_EQ(response.errCode_, MXM_OK);
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestShmUnmapAlreadyReleased)
{
    auto request = std::make_shared<ShmUnmapRequest>("default", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<CommonResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmUnmap(request.get(), response.get(), info);
    ASSERT_EQ(ret, MXM_OK);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmQueryMemFaultStatus)
{
    auto request = std::make_shared<ShmQueryMemFaultStatusRequest>("default");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmQueryMemFaultStatusResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
}

TEST_F(ShmIpcHandlerTestSuite, TestShmQueryMemFaultStatusFail)
{
    auto request = std::make_shared<ShmQueryMemFaultStatusRequest>("default");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmQueryMemFaultStatusResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
}

TEST_F(ShmIpcHandlerTestSuite, TestRegionLookupRegionList)
{
    MOCKER(ock::mxm::UbseMemAdapter::LookupRegionList).stubs().will(returnValue(0));
    auto request = std::make_shared<ShmLookRegionListRequest>("default", 0);
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmLookRegionListResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::RegionLookupRegionList(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestRegionLookupRegionListFail)
{
    MOCKER(ock::mxm::UbseMemAdapter::LookupRegionList).stubs().will(returnValue(-1));
    auto request = std::make_shared<ShmLookRegionListRequest>("default", 0);
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmLookRegionListResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::RegionLookupRegionList(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
}

TEST_F(ShmIpcHandlerTestSuite, TestRegionCreateRegion)
{
    SHMRegionDesc region = {};
    MOCKER(ock::mxm::UbseMemAdapter::LookupRegionList).stubs().will(returnValue(0));
    auto request = std::make_shared<ShmCreateRegionRequest>("default", region);
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmCreateRegionResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::RegionCreateRegion(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestRegionLookupRegionFail)
{
    auto request = std::make_shared<ShmLookupRegionRequest>("name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmLookupRegionResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::RegionLookupRegion(request.get(), response.get(), info);
    ASSERT_EQ(ret, MXM_ERR_REGION_NOT_EXIST);
}

TEST_F(ShmIpcHandlerTestSuite, TestRegionLookupRegionSuccess)
{
    SHMRegionDesc region = {};
    RegionInfo regionInfo{"name", 0, region};
    RegionManager::GetInstance().CreateRegionInfo(regionInfo);
    auto request = std::make_shared<ShmLookupRegionRequest>("name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmLookupRegionResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::RegionLookupRegion(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestRegionDestroyRegion)
{
    auto request = std::make_shared<ShmDestroyRegionRequest>("name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmDestroyRegionResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::RegionDestroyRegion(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmWriteLock)
{
    MOCKER_CPP(&ock::dlock_utils::UbsmLock::Lock,
               int32_t(*)(const std::string &name, bool isExclusive, ock::dlock_utils::LockUdsInfo &udsInfo))
        .stubs()
        .will(returnValue(0));

    auto request = std::make_shared<ShmWriteLockRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmWriteLockResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmWriteLock(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmWriteLockFail)
{
    MOCKER_CPP(&ock::dlock_utils::UbsmLock::Lock,
               int32_t(*)(const std::string &name, bool isExclusive, ock::dlock_utils::LockUdsInfo &udsInfo))
        .stubs()
        .will(returnValue(-1));
    auto request = std::make_shared<ShmWriteLockRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmWriteLockResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmWriteLock(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(response->errCode_, -1);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmReadLock)
{
    MOCKER_CPP(&ock::dlock_utils::UbsmLock::Lock,
               int32_t(*)(const std::string &name, bool isExclusive, ock::dlock_utils::LockUdsInfo &udsInfo))
        .stubs()
        .will(returnValue(0));

    auto request = std::make_shared<ShmReadLockRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmReadLockResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmReadLock(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmReadLockFail)
{
    MOCKER_CPP(&ock::dlock_utils::UbsmLock::Lock,
               int32_t(*)(const std::string &name, bool isExclusive, ock::dlock_utils::LockUdsInfo &udsInfo))
        .stubs()
        .will(returnValue(-1));

    auto request = std::make_shared<ShmReadLockRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmReadLockResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmReadLock(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(response->errCode_, -1);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmUnLock)
{
    MOCKER_CPP(&ock::dlock_utils::UbsmLock::Unlock,
               int32_t(*)(const std::string &name, const ock::dlock_utils::LockUdsInfo &udsInfo))
        .stubs()
        .will(returnValue(0));

    auto request = std::make_shared<ShmUnLockRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmUnLockResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmUnLock(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmUnLockFail)
{
    MOCKER_CPP(&ock::dlock_utils::UbsmLock::Unlock,
               int32_t(*)(const std::string &name, const ock::dlock_utils::LockUdsInfo &udsInfo))
        .stubs()
        .will(returnValue(-1));

    auto request = std::make_shared<ShmUnLockRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmUnLockResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmUnLock(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(response->errCode_, -1);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmQueryNodeNull)
{
    auto request = std::make_shared<QueryNodeRequest>(true);
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<QueryNodeResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmQueryNode(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(response->errCode_, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmAttachFail)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData,
               int (*)(const std::string &name, ock::mxm::ubse_user_info_t &ubsUserInfo))
        .stubs()
        .will(returnValue(0));
    auto request = std::make_shared<ShmAttachRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmAttachResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmAttach(request.get(), response.get(), info);
    ASSERT_EQ(ret, MXM_ERR_SHM_PERMISSION_DENIED);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmAttachFail02)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData,
               int (*)(const std::string &name, ock::mxm::ubse_user_info_t &ubsUserInfo))
        .stubs()
        .will(returnValue(-1));
    auto request = std::make_shared<ShmAttachRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmAttachResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmAttach(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmDetachFail)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData,
               int (*)(const std::string &name, ock::mxm::ubse_user_info_t &ubsUserInfo))
        .stubs()
        .will(returnValue(0));
    auto request = std::make_shared<ShmDetachRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<CommonResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmDetach(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmDetachFail01)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData,
               int (*)(const std::string &name, ock::mxm::ubse_user_info_t &ubsUserInfo))
        .stubs()
        .will(returnValue(-1));
    auto request = std::make_shared<ShmDetachRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<CommonResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmDetach(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmListLookup)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmListLookup,
               int (*)(const std::string &prefix, std::vector<ubsmem_shmem_desc_t> &nameList))
        .stubs()
        .will(returnValue(0));
    auto request = std::make_shared<ShmListLookupRequest>("regionName", "name");
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmListLookupResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmListLookup(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmLookup)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmLookup, int (*)(const std::string &name, ubsmem_shmem_info_t &shmInfo))
        .stubs()
        .will(returnValue(0));
    auto request = std::make_shared<ShmLookupRequest>();
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmLookupResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmLookup(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestShmLookupFail)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmLookup, int (*)(const std::string &name, ubsmem_shmem_info_t &shmInfo))
        .stubs()
        .will(returnValue(-1));
    auto request = std::make_shared<ShmLookupRequest>();
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<ShmLookupResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmLookup(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(response->errCode_, -1);
}

TEST_F(ShmIpcHandlerTestSuite, TestRpcQueryNodeInfo)
{
    MOCKER_CPP(&ock::rpc::service::RpcServer::SendMsg,
               int32_t(*)(uint16_t opCode, MsgBase * req, MsgBase * rsp, const std::string &nodeId))
        .stubs()
        .will(returnValue(0));
    auto request = std::make_shared<CommonRequest>();
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<RpcQueryInfoResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::RpcQueryNodeInfo(request.get(), response.get(), info);
    ASSERT_EQ(ret, 0);
}

TEST_F(ShmIpcHandlerTestSuite, TestOrdinaryDeleteRejectsMultiNodeMembers)
{
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData, int (*)(const std::string &, ock::mxm::ubse_user_info_t &))
        .expects(exactly(2))
        .will(invoke(GetMultiNodeMemberUserInfo));
    MOCKER_CPP(&SHMManager::GetMemoryUsersCountByName, int32_t(*)(const std::string &, size_t &)).expects(never());
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmDelete, int (*)(const std::string &, const ock::ubsm::AppContext &))
        .expects(never());
    MxmComUdsInfo info{};
    for (const auto &name : {"multi_delete", "multi_delete0101"}) {
        ShmDeleteRequest request;
        request.name_ = name;
        CommonResponse response;
        EXPECT_EQ(MxmServerMsgHandle::ShmDelete(&request, &response, info), MXM_ERR_UBSE_NOT_SUPPORTED);
        EXPECT_EQ(response.errCode_, MXM_ERR_UBSE_NOT_SUPPORTED);
    }
    GlobalMockObject::verify();
}

TEST_F(ShmIpcHandlerTestSuite, TestShmDelete)
{
    MOCKER_CPP(&SHMManager::GetMemoryUsersCountByName, int32_t(*)(const std::string &name, size_t &count))
        .stubs()
        .will(returnValue(-1));
    MOCKER_CPP(&ock::mxm::UbseMemAdapter::ShmGetUserData, int (*)(const std::string &name, ubs_mem_shm_desc_t &shmDes))
        .stubs()
        .will(invoke(ShmGetUserDataStub));
    MOCKER(memcpy_s).stubs().will(returnValue(0));
    auto request = std::make_shared<ShmDeleteRequest>();
    if (request == nullptr) {
        ASSERT_EQ(0, -1);
    }
    auto response = std::make_shared<CommonResponse>();
    if (response == nullptr) {
        ASSERT_EQ(0, -1);
    }
    MxmComUdsInfo info = {};
    auto ret = MxmServerMsgHandle::ShmDelete(request.get(), response.get(), info);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(response->errCode_, -1);
}

namespace {
struct PoolOperationGate {
    bool firstIsCreate;
    std::promise<void> firstEntered;
    std::promise<void> releaseFirst;
    std::atomic<bool> otherEntered{false};
};
PoolOperationGate *poolOperationGate = nullptr;

int WaitForPoolOperation(bool creating)
{
    if (creating == poolOperationGate->firstIsCreate) {
        poolOperationGate->firstEntered.set_value();
        poolOperationGate->releaseFirst.get_future().wait();
    } else {
        poolOperationGate->otherEntered.store(true);
    }
    return MXM_OK;
}

int PausePoolCreation(const ock::mxm::MultiNodeShmCreateParam &)
{
    return WaitForPoolOperation(true);
}

int PausePoolDeletion(const std::string &, const ock::ubsm::AppContext &)
{
    return WaitForPoolOperation(false);
}
} // namespace

TEST_F(ShmIpcHandlerTestSuite, MultiNodePoolCreateAndDeleteCannotInterleaveMemberOperations)
{
    for (const bool firstIsCreate : {true, false}) {
        PoolOperationGate gate;
        gate.firstIsCreate = firstIsCreate;
        poolOperationGate = &gate;
        MOCKER_CPP(&ock::mxm::UbseMemAdapter::CreateMultiNodeShmPool,
                   int (*)(const ock::mxm::MultiNodeShmCreateParam &))
            .expects(once())
            .will(invoke(PausePoolCreation));
        MOCKER_CPP(&ock::mxm::UbseMemAdapter::DeleteMultiNodeShmPool,
                   int (*)(const std::string &, const ock::ubsm::AppContext &))
            .expects(once())
            .will(invoke(PausePoolDeletion));
        auto invokeRequest = [](bool creating) {
            MxmComUdsInfo info{1234, 1000, 1000};
            CommonResponse response;
            if (creating) {
                MultiNodeShmCreateRequest request{
                    "pool_serialized", 8UL * 1024 * 1024, 0600, 0, 0, {"node1", "node2"}, {}};
                return MxmServerMsgHandle::ShmCreateMultiNodePool(&request, &response, info);
            }
            MultiNodeShmDeleteRequest request{"pool_serialized"};
            return MxmServerMsgHandle::ShmDeleteMultiNodePool(&request, &response, info);
        };
        auto entered = gate.firstEntered.get_future();
        auto first = std::async(std::launch::async, invokeRequest, firstIsCreate);
        const auto enteredStatus = entered.wait_for(std::chrono::seconds(2));
        EXPECT_EQ(enteredStatus, std::future_status::ready);
        if (enteredStatus != std::future_status::ready) {
            gate.releaseFirst.set_value();
            first.get();
            poolOperationGate = nullptr;
            return;
        }
        std::promise<void> secondAttempted;
        auto attempted = secondAttempted.get_future();
        auto second = std::async(std::launch::async, [&] {
            secondAttempted.set_value();
            return invokeRequest(!firstIsCreate);
        });
        EXPECT_EQ(attempted.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_EQ(second.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
        EXPECT_FALSE(gate.otherEntered.load());
        gate.releaseFirst.set_value();
        EXPECT_EQ(first.get(), MXM_OK);
        EXPECT_EQ(second.get(), MXM_OK);
        EXPECT_TRUE(gate.otherEntered.load());
        poolOperationGate = nullptr;
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
}
