/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <mockcpp/mockcpp.hpp>
#include <thread>
#include "BorrowAppMetaMgr.h"
#include "ShmMetaDataMgr.h"
#include "ipc_proxy.h"
#include "rack_mem_fd_map.h"
#include "rack_mem_lib.h"
#define private public
#include "UbseMemExecutor.h"
#include "fork_coordinator.h"
#undef private
#include "ubs_mem.h"
#include "fork_resources.h"

#define MOCKER_CPP(api, TT) (MOCKCPP_NS::mockAPI((#api), (reinterpret_cast<TT>(api))))
using namespace ock::mxmd;
using namespace ock::common;

class ForkCoordinatorDesignTest : public testing::Test {
protected:
    ForkCoordinator &control = ForkCoordinator::Instance();
    void SetUp() override
    {
        control.phase_ = ForkCoordinator::Phase::RUNNING;
        control.admission_ = 0;
        control.SetInitialized(true);
        MOCKER_CPP(&RackMemLib::GetIsInitilized, bool (*)()).stubs().will(returnValue(true));
        MOCKER_CPP(&RackMemLib::ForkSupported, bool (*)()).stubs().will(returnValue(true));
        MOCKER_CPP(&UbseMemExecutor::ForkPrepare, int (*)()).stubs().will(returnValue(static_cast<int>(UBSM_OK)));
    }
    void TearDown() override
    {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
        control.phase_ = ForkCoordinator::Phase::RUNNING;
        control.admission_ = 0;
        control.SetInitialized(false);
    }
};

class ForkPrepareOrderTest : public ForkCoordinatorDesignTest {
protected:
    void SetUp() override
    {
        ForkCoordinatorDesignTest::SetUp();
        mockcpp::GlobalMockObject::reset();
        MOCKER_CPP(&RackMemLib::GetIsInitilized, bool (*)()).stubs().will(returnValue(true));
        MOCKER_CPP(&RackMemLib::ForkSupported, bool (*)()).stubs().will(returnValue(true));
    }
};

static bool ipcStoppedBeforeUbse = false;
static uint32_t MarkIpcStopped()
{
    ipcStoppedBeforeUbse = true;
    return UBSM_OK;
}
static int PrepareUbseAfterIpcStop()
{
    EXPECT_TRUE(ipcStoppedBeforeUbse);
    return UBSM_OK;
}

TEST_F(ForkPrepareOrderTest, IpcSuspendsBeforeUbseStops)
{
    ipcStoppedBeforeUbse = false;
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).expects(once()).will(invoke(MarkIpcStopped));
    MOCKER_CPP(&UbseMemExecutor::ForkPrepare, int (*)()).expects(once()).will(invoke(PrepareUbseAfterIpcStop));
    MOCKER(IpcProxy::Resume).expects(once()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER_CPP(&UbseMemExecutor::ForkResume, int (*)()).stubs().will(returnValue(static_cast<int>(UBSM_OK)));
    ASSERT_EQ(ubsmem_fork_prepare(), UBSM_OK);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
}

TEST_F(ForkPrepareOrderTest, SuspendFailureDoesNotStopOrRestartUbse)
{
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).expects(once()).will(returnValue(static_cast<uint32_t>(MXM_ERR_IPC_SYNC_CALL)));
    MOCKER_CPP(&UbseMemExecutor::ForkPrepare, int (*)()).expects(never());
    MOCKER_CPP(&UbseMemExecutor::ForkResume, int (*)()).expects(never());
    MOCKER(IpcProxy::Resume).expects(once()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    EXPECT_EQ(ubsmem_fork_prepare(), GetErrCode(MXM_ERR_IPC_SYNC_CALL));
    EXPECT_EQ(control.phase_.load(), ForkCoordinator::Phase::RUNNING);
    EXPECT_EQ(control.admission_.load(), 0U);
}

TEST_F(ForkPrepareOrderTest, UbsePrepareFailureRestoresSuspendedIpc)
{
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).expects(once()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER_CPP(&UbseMemExecutor::ForkPrepare, int (*)())
        .expects(once())
        .will(returnValue(static_cast<int>(UBSM_ERR_NOT_SUPPORTED)));
    MOCKER_CPP(&UbseMemExecutor::ForkResume, int (*)()).expects(never());
    MOCKER(IpcProxy::Resume).expects(once()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    EXPECT_EQ(ubsmem_fork_prepare(), UBSM_ERR_NOT_SUPPORTED);
    EXPECT_EQ(control.phase_.load(), ForkCoordinator::Phase::RUNNING);
    EXPECT_EQ(control.admission_.load(), 0U);
}

TEST_F(ForkCoordinatorDesignTest, IdleParentOnlyConfirms)
{
    MOCKER(IpcProxy::Resume).expects(never());
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
    std::thread other([] { EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK); });
    other.join();
    control.SetInitialized(false);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_ERR_MEMLIB);
}

TEST_F(ForkCoordinatorDesignTest, ComponentBusyDoesNotMasqueradeAsContention)
{
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(MXM_ERR_NAME_BUSY)));
    EXPECT_EQ(ubsmem_fork_prepare(), UBSM_ERR_MEMLIB);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
    EXPECT_EQ(control.admission_.load(), 0U);
}

static unsigned int clockCalls = 0;
static std::chrono::steady_clock::time_point ExpiredClock() noexcept
{
    return std::chrono::steady_clock::time_point(std::chrono::seconds(clockCalls++ == 0 ? 0 : 31));
}
TEST_F(ForkCoordinatorDesignTest, DrainTimeoutPreservesExistingAdmission)
{
    std::atomic<bool> entered{false}, release{false};
    std::thread active([&] {
        ForkApiGuard guard;
        entered.store(guard.Entered());
        while (!release.load()) {
            std::this_thread::yield();
        }
    });
    while (!entered.load()) {
        std::this_thread::yield();
    }
    clockCalls = 0;
    MOCKER_CPP(std::chrono::steady_clock::now, std::chrono::steady_clock::time_point(*)())
        .stubs()
        .will(invoke(static_cast<std::chrono::steady_clock::time_point (*)()>(ExpiredClock)));
    EXPECT_EQ(ubsmem_fork_prepare(), UBSM_ERR_MEMLIB);
    EXPECT_EQ(control.admission_.load(), 1U);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
    release.store(true);
    active.join();
    EXPECT_EQ(control.admission_.load(), 0U);
}

TEST_F(ForkCoordinatorDesignTest, ClosedAdmissionRejectsNestedAndCallbackCalls)
{
    {
        ForkApiGuard outer;
        ASSERT_TRUE(outer.Entered());
        control.admission_.fetch_or(ForkCoordinator::CLOSED);
        ForkApiGuard nested;
        EXPECT_FALSE(nested.Entered());
        EXPECT_EQ(control.admission_.load(), ForkCoordinator::CLOSED | 1U);
        ForkCallbackScope callback;
        ForkApiGuard callbackApi;
        EXPECT_FALSE(callbackApi.Entered());
    }
    EXPECT_EQ(control.admission_.load(), ForkCoordinator::CLOSED);
}

TEST_F(ForkCoordinatorDesignTest, CallbackRejectsLifecycleWithoutAnActiveApi)
{
    for (const bool isLogger : {false, true}) {
        ForkCallbackScope callback(isLogger);
        EXPECT_EQ(ubsmem_fork_prepare(), UBSM_ERR_BUSY);
        EXPECT_EQ(ubsmem_fork_parent(), UBSM_ERR_BUSY);
        EXPECT_EQ(ubsmem_fork_child(), UBSM_ERR_BUSY);
        EXPECT_EQ(control.admission_.load(), 0U);
        EXPECT_EQ(control.phase_.load(), ForkCoordinator::Phase::RUNNING);
    }
}

TEST_F(ForkCoordinatorDesignTest, ParentRetrySkipsCompletedIpcResume)
{
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::Resume).expects(once()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER_CPP(&UbseMemExecutor::ForkResume, int (*)())
        .stubs()
        .will(returnValue(static_cast<int>(UBSM_ERR_UBSE)))
        .then(returnValue(static_cast<int>(UBSM_OK)));
    ASSERT_EQ(ubsmem_fork_prepare(), UBSM_OK);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_ERR_UBSE);
    EXPECT_EQ(ubsmem_init_attributes(nullptr), UBSM_ERR_BUSY);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
}

TEST_F(ForkCoordinatorDesignTest, ForeignThreadCannotRecoverPreparedParent)
{
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::Resume).expects(once()).will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER_CPP(&UbseMemExecutor::ForkResume, int (*)()).stubs().will(returnValue(static_cast<int>(UBSM_OK)));
    ASSERT_EQ(ubsmem_fork_prepare(), UBSM_OK);
    std::thread foreign([] { EXPECT_EQ(ubsmem_fork_parent(), UBSM_ERR_PARAM_INVALID); });
    foreign.join();
    EXPECT_EQ(control.phase_.load(), ForkCoordinator::Phase::PREPARED);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
}

static unsigned int childResetCalls = 0;
static void CountChildReset()
{
    ++childResetCalls;
}

TEST_F(ForkCoordinatorDesignTest, ChildRetryRepeatsResetOnlyUntilLoggerIsReady)
{
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    ASSERT_EQ(ubsmem_fork_prepare(), UBSM_OK);
    const pid_t childPid = getpid() + 1;
    MOCKER_CPP(getpid, pid_t(*)()).stubs().will(returnValue(childPid));
    childResetCalls = 0;
    MOCKER_CPP(&ShmMetaDataMgr::ForkChildResetNoLock, void (*)()).expects(exactly(2)).will(invoke(CountChildReset));
    MOCKER_CPP(&BorrowAppMetaMgr::ForkChildResetNoLock, void (*)()).expects(exactly(2)).will(invoke(CountChildReset));
    MOCKER_CPP(&UbseMemExecutor::ForkChildReset, void (*)()).expects(exactly(2)).will(invoke(CountChildReset));
    MOCKER(ubsmem::log::UbsmemLoggerManager::ForkChildReset)
        .expects(exactly(2))
        .will(returnValue(-1))
        .then(returnValue(0));
    MOCKER(IpcProxy::ForkChildInitialize)
        .expects(exactly(2))
        .will(returnValue(static_cast<uint32_t>(MXM_ERR_IPC_INIT_CALL)))
        .then(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER_CPP(&UbseMemExecutor::ForkResume, int (*)()).expects(once()).will(returnValue(static_cast<int>(UBSM_OK)));
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_ERR_PARAM_INVALID);
    EXPECT_EQ(ubsmem_fork_child(), UBSM_ERR_MALLOC_FAIL);
    EXPECT_FALSE(control.childReset_);
    EXPECT_EQ(ubsmem_init_attributes(nullptr), UBSM_ERR_BUSY);
    EXPECT_EQ(ubsmem_fork_child(), GetErrCode(MXM_ERR_IPC_INIT_CALL));
    EXPECT_TRUE(control.childReset_);
    EXPECT_EQ(ubsmem_init_attributes(nullptr), UBSM_ERR_BUSY);
    EXPECT_EQ(ubsmem_fork_child(), UBSM_OK);
    EXPECT_EQ(childResetCalls, 6U);
    EXPECT_EQ(control.phase_.load(), ForkCoordinator::Phase::RUNNING);
}

TEST_F(ForkCoordinatorDesignTest, QualifiedParentBackendBusyIsARecoveryFailure)
{
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::Resume).expects(once()).will(returnValue(static_cast<uint32_t>(MXM_ERR_NAME_BUSY)));
    ASSERT_EQ(ubsmem_fork_prepare(), UBSM_OK);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_ERR_MEMLIB);
    EXPECT_EQ(ubsmem_init_attributes(nullptr), UBSM_ERR_BUSY);
}

TEST(ForkLoggerDesignTest, ClearsInheritedCallbackAndAllowsExplicitNewRegistration)
{
    ubsmem::log::UbsmemLoggerManager logger;
    auto inherited = +[](int, const char *) {
    };
    logger.SetExternLogCallback(inherited);
    ASSERT_EQ(logger.GetExternLogCallback(), inherited);
    ASSERT_EQ(logger.ResetAfterFork(), 0);
    EXPECT_EQ(logger.GetExternLogCallback(), nullptr);
    logger.SetExternLogCallback(inherited);
    EXPECT_EQ(logger.GetExternLogCallback(), inherited);
}

static int initializes = 0, registrations = 0, callbacks = 0, stops = 0;
static int32_t ClientInitialize(const char *)
{
    ++initializes;
    return 0;
}
static void ClientStop()
{
    ++stops;
}
static int32_t RegisterFault(ubs_mem_shm_fault_handler)
{
    ++registrations;
    return 0;
}
static int32_t UserFault(const char *, ubsmem_fault_type_t)
{
    ++callbacks;
    return 0;
}

TEST(ForkUbseDesignTest, FinalizeStopsClientAndParentRestoresSubscriptionOnce)
{
    UbseMemExecutor executor;
    const auto previous = executor.ShmFaultFunc.load();
    executor.initialized_ = true;
    executor.clientInitialize_ = ClientInitialize;
    executor.clientFinalize_ = ClientStop;
    executor.UbseFaultEventRegisterFunc = RegisterFault;
    executor.ShmFaultFunc = UserFault;
    initializes = registrations = stops = 0;
    EXPECT_EQ(executor.ForkPrepare(), UBSM_OK);
    EXPECT_EQ(executor.ForkPrepare(), UBSM_OK);
    EXPECT_EQ(stops, 1);
    EXPECT_TRUE(executor.restartAfterFork_);
    EXPECT_EQ(executor.ForkResume(), UBSM_OK);
    EXPECT_EQ(executor.ForkResume(), UBSM_OK);
    EXPECT_EQ(initializes, 1);
    EXPECT_EQ(registrations, 1);
    EXPECT_EQ(executor.ShmFaultFunc.load(), UserFault);
    EXPECT_FALSE(executor.restartAfterFork_);
    executor.ShmFaultFunc = previous;
    executor.initialized_ = false;
}

TEST(ForkUbseDesignTest, MissingLifecycleSymbolRejectsBeforeStopping)
{
    UbseMemExecutor executor;
    executor.initialized_ = true;
    executor.clientFinalize_ = ClientStop;
    stops = 0;
    EXPECT_EQ(executor.ForkPrepare(), UBSM_ERR_NOT_SUPPORTED);
    EXPECT_EQ(stops, 0);
    executor.initialized_ = false;
}

static int32_t FailFirstClientInitialize(const char *)
{
    return ++initializes == 1 ? -1 : 0;
}

static int32_t FailFirstRegisterFault(ubs_mem_shm_fault_handler)
{
    return ++registrations == 1 ? -1 : 0;
}

TEST(ForkUbseDesignTest, FailedInitializeIsFinalizedBeforeRetry)
{
    UbseMemExecutor executor;
    const auto previous = executor.ShmFaultFunc.load();
    executor.initialized_ = true;
    executor.clientInitialize_ = FailFirstClientInitialize;
    executor.clientFinalize_ = ClientStop;
    executor.UbseFaultEventRegisterFunc = RegisterFault;
    executor.ShmFaultFunc = UserFault;
    initializes = registrations = stops = 0;
    EXPECT_EQ(executor.ForkPrepare(), UBSM_OK);
    EXPECT_EQ(executor.ForkResume(), UBSM_ERR_UBSE);
    EXPECT_EQ(stops, 2);
    EXPECT_FALSE(executor.clientRestarted_);
    EXPECT_EQ(registrations, 0);
    EXPECT_EQ(executor.ForkResume(), UBSM_OK);
    EXPECT_EQ(initializes, 2);
    EXPECT_EQ(registrations, 1);
    executor.ShmFaultFunc = previous;
    executor.initialized_ = false;
}

TEST(ForkUbseDesignTest, SubscriptionRetryDoesNotReinitializeClient)
{
    UbseMemExecutor executor;
    const auto previous = executor.ShmFaultFunc.load();
    executor.initialized_ = true;
    executor.clientInitialize_ = ClientInitialize;
    executor.clientFinalize_ = ClientStop;
    executor.UbseFaultEventRegisterFunc = FailFirstRegisterFault;
    executor.ShmFaultFunc = UserFault;
    initializes = registrations = stops = 0;
    EXPECT_EQ(executor.ForkPrepare(), UBSM_OK);
    EXPECT_EQ(executor.ForkResume(), UBSM_ERR_UBSE);
    EXPECT_TRUE(executor.clientRestarted_);
    EXPECT_EQ(executor.ForkResume(), UBSM_OK);
    EXPECT_EQ(initializes, 1);
    EXPECT_EQ(stops, 1);
    EXPECT_EQ(registrations, 2);
    executor.ShmFaultFunc = previous;
    executor.initialized_ = false;
}

TEST(ForkUbseDesignTest, ChildDoesNotReplayParentOrEarlierChildSubscription)
{
    UbseMemExecutor executor;
    const auto previous = executor.ShmFaultFunc.load();
    executor.initialized_ = true;
    executor.clientInitialize_ = ClientInitialize;
    executor.clientFinalize_ = ClientStop;
    executor.UbseFaultEventRegisterFunc = RegisterFault;
    executor.ShmFaultFunc = UserFault;
    initializes = registrations = callbacks = stops = 0;
    for (unsigned int generation = 0; generation < 2; ++generation) {
        ASSERT_EQ(executor.ForkPrepare(), UBSM_OK);
        executor.ForkChildReset();
        ASSERT_EQ(executor.ForkResume(), UBSM_OK);
        EXPECT_EQ(registrations, static_cast<int>(generation));
        EXPECT_EQ(executor.ShmFaultFunc.load(), nullptr);
        ASSERT_EQ(executor.RegisterShmFaultEvent(UserFault), 0);
        EXPECT_EQ(executor.UbseShmFaultHandlerFunc("child", 0, static_cast<ubs_mem_fault_type_t>(0)), 0);
    }
    EXPECT_EQ(stops, 2);
    EXPECT_EQ(initializes, 2);
    EXPECT_EQ(registrations, 2);
    EXPECT_EQ(callbacks, 2);
    executor.ShmFaultFunc = previous;
    executor.initialized_ = false;
}

static ubsmem::log::UbsmemLoggerManager *callbackLogger = nullptr;
static unsigned int logCallbackCalls = 0;
static void ReentrantLogger(int, const char *)
{
    ++logCallbackCalls;
    ubsmem::log::UbsmemLoggerEntry nested(ubsmem::log::UbsmemLogLevel::ERROR, __FILE__, __func__, __LINE__);
    callbackLogger->Push(std::move(nested));
}

TEST(ForkLoggerDesignTest, CallbackLoggingReusesExistingWriterWithoutRecursion)
{
    ubsmem::log::UbsmemLoggerManager logger;
    ASSERT_EQ(logger.ResetAfterFork(), 0);
    callbackLogger = &logger;
    logCallbackCalls = 0;
    logger.SetExternLogCallback(ReentrantLogger);
    testing::internal::CaptureStdout();
    ubsmem::log::UbsmemLoggerEntry entry(ubsmem::log::UbsmemLogLevel::ERROR, __FILE__, __func__, __LINE__);
    {
        // An ordinary fault callback must still deliver logs to the configured external logger.
        ForkCallbackScope faultCallback;
        logger.Push(std::move(entry));
    }
    const auto output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(logCallbackCalls, 1U);
    EXPECT_FALSE(output.empty());
    callbackLogger = nullptr;
}

TEST_F(ForkCoordinatorDesignTest, UnexpectedMappingAddressLeavesRollbackToCaller)
{
    MOCKER(ock::ubsm::SystemAdapter::MemoryMap).stubs().will(returnValue(reinterpret_cast<void *>(0x1000)));
    MOCKER(ock::ubsm::SystemAdapter::MemoryUnMap).expects(never());
    MOCKER_CPP(madvise, int (*)(void *, size_t, int)).stubs().will(returnValue(0));
    void *address = reinterpret_cast<void *>(0x2000);
    EXPECT_EQ(RackMemFdMap::GetInstance().MapForEachFd(address, 4096, -1, PROT_READ, MAP_SHARED | MAP_FIXED),
              MXM_ERR_MMAP_VA_FAILED);
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::Resume).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER_CPP(&UbseMemExecutor::ForkResume, int (*)()).stubs().will(returnValue(static_cast<int>(UBSM_OK)));
    EXPECT_EQ(ubsmem_fork_prepare(), UBSM_OK);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
}

TEST_F(ForkCoordinatorDesignTest, CloseFailureDoesNotPermanentlyRejectLaterPrepare)
{
    MOCKER(ock::ubsm::SystemAdapter::Close).expects(once()).will(returnValue(-1));
    {
        TemporaryObmmFd descriptor(42);
    }
    EXPECT_EQ(temporaryObmmFds.load(), 0U);
    MOCKER(IpcProxy::PrepareFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::SuspendForFork).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER(IpcProxy::Resume).stubs().will(returnValue(static_cast<uint32_t>(UBSM_OK)));
    MOCKER_CPP(&UbseMemExecutor::ForkResume, int (*)()).stubs().will(returnValue(static_cast<int>(UBSM_OK)));
    EXPECT_EQ(ubsmem_fork_prepare(), UBSM_OK);
    EXPECT_EQ(ubsmem_fork_parent(), UBSM_OK);
}
