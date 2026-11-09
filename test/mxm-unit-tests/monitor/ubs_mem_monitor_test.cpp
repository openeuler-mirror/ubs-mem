/*
Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */
#include "comm_def.h"
#include "gtest/gtest.h"
#include "mockcpp/mockcpp.hpp"
#include "mxm_ipc_server_interface.h"
#include "mxm_shm/ipc_handler.h"
#include "ubs_common_config.h"
#define private public
#include "mls_manager.h"
#include "mls_repository.h"
#include "shm_manager.h"
#include "ubs_mem_leak_cleaner.h"
#include "ubs_mem_monitor.h"

#undef private

#define MOCKER_CPP(api, TT) MOCKCPP_NS::mockAPI(#api, reinterpret_cast<TT>(api))

namespace UT {
class UbseMemMonitorDlTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

void MXMSetLinkEventHandlerStub(const ock::com::MXMLinkEventHandler &handler)
{
    return;
}

using namespace ock::lease::service;
using namespace ock::mxm;
using namespace ock::ubsm;

static uint32_t rollbackRetryCalls{};

static int RetryRollbackForMonitor(const std::string &name, const ock::com::MxmComUdsInfo &, uint64_t referenceId)
{
    EXPECT_EQ(name, "monitor_rollback");
    EXPECT_TRUE(referenceId == 101 || referenceId == 102);
    EXPECT_TRUE(UBSMemMonitor::GetInstance().GetDelayRemoveRecord(DelayRemovedKey{name}));
    return ++rollbackRetryCalls == 1 ? ock::common::MXM_ERR_UBSE_INNER : 0;
}

TEST_F(UbseMemMonitorDlTest, RollbackTasksRetryIndependentlyAndKeepNameBusy)
{
    auto &monitor = UBSMemMonitor::GetInstance();
    const auto previousKeys = monitor.delayedRemoveKeys;
    const auto previousNames = monitor.delayedRemoveKeysByName;
    const auto previousRunning = monitor.running;
    monitor.delayedRemoveKeys.clear();
    monitor.delayedRemoveKeysByName.clear();
    monitor.running = true;
    rollbackRetryCalls = 0;
    DelayRemovedKey first{"monitor_rollback", 0, false, AppContext{1234, 1000, 1000}, false, false, false};
    first.shmRollbackReferenceId = 101;
    first.expiresTime = 0;
    auto second = first;
    second.shmRollbackReferenceId = 102;
    second.appCtx.pid = 5678;
    second.expiresTime += ONE_MINUTE_MS + std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count();
    monitor.AddDelayRemoveRecord(first);
    monitor.AddDelayRemoveRecord(second);
    auto duplicate = first;
    duplicate.expiresTime = second.expiresTime;
    monitor.AddDelayRemoveRecord(duplicate);
    EXPECT_EQ(monitor.delayedRemoveKeys.size(), 2);
    MOCKER(ock::share::service::MxmServerMsgHandle::RollbackMultiNodeShmMember)
        .expects(exactly(3))
        .will(invoke(RetryRollbackForMonitor));
    MOCKER_CPP(&UBSMemLeakCleaner::CleanShareMemoryLeakInner, int (*)()).expects(never());
    monitor.RemoveDelayBorrows();
    EXPECT_EQ(monitor.delayedRemoveKeys.size(), 2);
    EXPECT_TRUE(monitor.GetDelayRemoveRecord(DelayRemovedKey{first.name}));
    const auto pending = monitor.delayedRemoveKeys;
    monitor.delayedRemoveKeys.clear();
    for (auto key : pending) {
        key.expiresTime = 0;
        monitor.delayedRemoveKeys.insert(key);
    }
    monitor.RemoveDelayBorrows();
    EXPECT_TRUE(monitor.delayedRemoveKeys.empty());
    EXPECT_FALSE(monitor.GetDelayRemoveRecord(DelayRemovedKey{first.name}));
    monitor.delayedRemoveKeys = previousKeys;
    monitor.delayedRemoveKeysByName = previousNames;
    monitor.running = previousRunning;
    GlobalMockObject::verify();
}

TEST_F(UbseMemMonitorDlTest, UbseMemMonitorDlTest_Initialize)
{
    MOCKER(ock::com::ipc::MXMSetLinkEventHandler).stubs().will(invoke(MXMSetLinkEventHandlerStub));
    MOCKER(ock::ubsm::ProcessAlive).stubs().will(returnValue(true));
    auto ret = ock::ubsm::UBSMemMonitor::GetInstance().Initialize();
    EXPECT_EQ(ret, 0);
    ret = ock::ubsm::UBSMemMonitor::GetInstance().Initialize();
    EXPECT_EQ(ret, 0);

    ock::ubsm::UBSMemMonitor::GetInstance().Destroy();
    GlobalMockObject::reset();
}

TEST_F(UbseMemMonitorDlTest, UbseMemMonitorDlTest_InitializeFailedShm)
{
    MOCKER(ock::com::ipc::MXMSetLinkEventHandler).stubs().will(invoke(MXMSetLinkEventHandlerStub));
    MOCKER_CPP(&UBSMemLeakCleaner::CleanShareMemoryLeakWhenStart, int (*)()).stubs().will(returnValue(-1));
    auto ret = ock::ubsm::UBSMemMonitor::GetInstance().Initialize();
    EXPECT_EQ(ret, -1);
    GlobalMockObject::reset();
}

TEST_F(UbseMemMonitorDlTest, UbseMemMonitorDlTest_InitializeFailedLease)
{
    MOCKER(ock::com::ipc::MXMSetLinkEventHandler).stubs().will(invoke(MXMSetLinkEventHandlerStub));
    MOCKER_CPP(&UBSMemLeakCleaner::CleanLeaseMemoryLeakWhenStart, int (*)()).stubs().will(returnValue(-1));
    auto ret = ock::ubsm::UBSMemMonitor::GetInstance().Initialize();
    EXPECT_EQ(ret, -1);
    GlobalMockObject::reset();
}

std::unordered_set<pid_t> GetAllUsersStub()
{
    std::unordered_set<pid_t> allPids;
    allPids.insert(1024);
    return allPids;
}

TEST_F(UbseMemMonitorDlTest, UbseMemMonitorDlTest_CleanLeaseMemoryLeakInnerFaild)
{
    MOCKER(ock::com::ipc::MXMSetLinkEventHandler).stubs().will(invoke(MXMSetLinkEventHandlerStub));
    MOCKER(ock::ubsm::ProcessAlive).stubs().will(returnValue(true));
    MOCKER_CPP(&UBSMemLeakCleaner::CleanLeaseMemoryLeakInner, int (*)()).stubs().will(returnValue(-1));
    MOCKER_CPP(&ock::share::service::SHMManager::GetAllUsers, std::unordered_set<pid_t>(*)())
        .stubs()
        .will(invoke(GetAllUsersStub));
    auto ret = ock::ubsm::UBSMemMonitor::GetInstance().Initialize();
    EXPECT_EQ(ret, 0);
    ret = ock::ubsm::UBSMemMonitor::GetInstance().Initialize();
    EXPECT_EQ(ret, 0);

    ock::ubsm::UBSMemMonitor::GetInstance().Destroy();
    GlobalMockObject::reset();
}

TEST_F(UbseMemMonitorDlTest, UbseMemMonitorDlTest_RemoveDelayBorrows)
{
    MOCKER(ock::com::ipc::MXMSetLinkEventHandler).stubs().will(invoke(MXMSetLinkEventHandlerStub));

    ock::ubsm::AppContext appContext;
    DelayRemovedKey removedKey{"ut_monitor", 0, true, appContext, false, false, false};
    UBSMemMonitor::GetInstance().AddDelayRemoveRecord(removedKey);

    DelayRemovedKey removedKeyShm{"ut_monitor_shm", 0, false, appContext, false, false, false};
    UBSMemMonitor::GetInstance().AddDelayRemoveRecord(removedKeyShm);

    auto ret = ock::ubsm::UBSMemMonitor::GetInstance().Initialize();
    EXPECT_EQ(ret, 0);

    ock::ubsm::UBSMemMonitor::GetInstance().Destroy();
    GlobalMockObject::reset();
}

TEST_F(UbseMemMonitorDlTest, UbseMemMonitorDlTest_RemoveDelayBorrowsTimeOut)
{
    MOCKER(ock::com::ipc::MXMSetLinkEventHandler).stubs().will(invoke(MXMSetLinkEventHandlerStub));

    ock::ubsm::AppContext appContext;
    DelayRemovedKey removedKey{"ut_monitor", 0, true, appContext, true, false, false};
    UBSMemMonitor::GetInstance().AddDelayRemoveRecord(removedKey);

    DelayRemovedKey removedKeyShm{"ut_monitor_shm", 0, false, appContext, false, true, true};
    UBSMemMonitor::GetInstance().AddDelayRemoveRecord(removedKeyShm);

    auto ret = ock::ubsm::UBSMemMonitor::GetInstance().Initialize();
    EXPECT_EQ(ret, 0);

    ock::ubsm::UBSMemMonitor::GetInstance().Destroy();
    GlobalMockObject::reset();
}

TEST_F(UbseMemMonitorDlTest, FirstFreeFailAndRemoveDelayBorrowsSuccess)
{
    MLSManager::GetInstance().isEnableLeaseBuffered_ = false;
    MLSMemInfo memory = {.name = "DelayRemoveSuccess",
                         .appContext = {12345, 0, 0},
                         .size = 134217728,
                         .numaId = -1,
                         .memIds = {6},
                         .unitSize = 134217728,
                         .perflevel = L0};

    MLSManager::GetInstance().PreAddUsedMem(memory.name, memory.size, memory.appContext, false, 0);
    MLSManager::GetInstance().FinishAddUsedMem(memory.name, -1, memory.unitSize, 0, {6});
    UBSMemMonitor::GetInstance().delayedRemoveKeys.clear();

    MOCKER_CPP(&UbseMemAdapter::FdPermissionChange, int (*)(const std::string &, const AppContext &, mode_t))
        .stubs()
        .will(returnValue(0))
        .then(returnValue(1)); // 修改权限第一次成功，第二次失败
    MOCKER_CPP(&UbseMemAdapter::LeaseFree, int (*)(const std::string &, bool)).stubs().will(returnValue(0));
    MOCKER_CPP(&UBSMemLeakCleaner::SHMProcessDeadProcess, int (*)(pid_t)).stubs().will(returnValue(0));
    UBSMemMonitor::GetInstance().ClientProcessExited(12345);

    EXPECT_EQ(UBSMemMonitor::GetInstance().delayedRemoveKeys.size(), 1);
    UBSMemMonitor::GetInstance().running = true;
    DelayRemovedKey key = *UBSMemMonitor::GetInstance().delayedRemoveKeys.begin();
    key.expiresTime = 0; // 避免因等待时间不够
    UBSMemMonitor::GetInstance().delayedRemoveKeys.erase(UBSMemMonitor::GetInstance().delayedRemoveKeys.begin());
    UBSMemMonitor::GetInstance().delayedRemoveKeys.insert(key);
    UBSMemMonitor::GetInstance().delayedRemoveKeysByName.insert(key);
    UBSMemMonitor::GetInstance().RemoveDelayBorrows();
    EXPECT_EQ(UBSMemMonitor::GetInstance().delayedRemoveKeys.size(), 0);
    UBSMemMonitor::GetInstance().running = false;
    MLSManager::GetInstance().isEnableLeaseBuffered_ = true;
}
} // namespace UT
