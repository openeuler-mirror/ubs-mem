/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#include "fork_coordinator.h"
#include "ubs_mem.h"
#include "fork_resources.h"

#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <thread>
#include "BorrowAppMetaMgr.h"
#include "ShmMetaDataMgr.h"
#include "UbseMemExecutor.h"
#include "ipc_proxy.h"
#include "rack_mem_lib.h"

namespace ock::mxmd {
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static constexpr int API_DRAIN_TIMEOUT_SECONDS = 30;
static thread_local uint64_t preparedGeneration = 0;

ForkCoordinator ForkCoordinator::instance_{};
ForkCoordinator &ForkCoordinator::Instance() noexcept
{
    return instance_;
}

void ForkCoordinator::SetInitialized(bool initialized)
{
    runtimePid_.store(initialized ? getpid() : 0);
}

bool ForkCoordinator::EnterApi()
{
    auto current = admission_.load();
    do {
        if ((current & CLOSED) != 0) {
            return false;
        }
    } while (!admission_.compare_exchange_weak(current, current + 1));
    return true;
}

void ForkCoordinator::LeaveApi()
{
    admission_.fetch_sub(1);
}

void ForkCoordinator::Open()
{
    phase_.store(Phase::RUNNING);
    admission_.store(0);
}

int ForkCoordinator::Prepare()
{
    if (forkCallbackDepth != 0) {
        return UBSM_ERR_BUSY;
    }
    if ((admission_.fetch_or(CLOSED) & CLOSED) != 0) {
        return UBSM_ERR_BUSY;
    }
    ownerPid_ = getpid();
    preparedGeneration = ++generation_;
    childReset_ = false;
    parentIpcReady_ = false;
    phase_.store(Phase::PREPARING);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(API_DRAIN_TIMEOUT_SECONDS);
    while (admission_.load() != CLOSED) {
        if (std::chrono::steady_clock::now() >= deadline) {
            // Keep the existing count when cancelling a drain.
            DBG_LOGERROR("Timed out draining SDK calls before fork, timeoutSeconds="
                         << API_DRAIN_TIMEOUT_SECONDS << ", admission=" << admission_.load());
            phase_.store(Phase::RUNNING);
            admission_.fetch_and(~CLOSED);
            return UBSM_ERR_MEMLIB;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!RackMemLib::GetInstance().GetIsInitilized()) {
        DBG_LOGERROR("Fork prepare rejected because the SDK is not initialized");
        Open();
        return UBSM_ERR_MEMLIB;
    }
    // Operation-local device descriptors must be closed before fork.
    if (temporaryObmmFds.load() != 0) {
        DBG_LOGERROR(
            "Fork prepare rejected because temporary OBMM descriptors remain open, count=" << temporaryObmmFds.load());
        Open();
        return UBSM_ERR_OBMM;
    }
    if (!RackMemLib::GetInstance().ForkSupported()) {
        DBG_LOGERROR("Fork prepare rejected because the SDK tracer is active");
        Open();
        return UBSM_ERR_NOT_SUPPORTED;
    }
    auto ret = GetErrCode(IpcProxy::PrepareFork());
    if (ret != UBSM_OK) {
        const auto failureResult = ret == UBSM_ERR_BUSY ? UBSM_ERR_MEMLIB : ret;
        DBG_LOGERROR("IPC preflight check before fork failed, error=" << ret);
        Open();
        return failureResult;
    }
    ret = GetErrCode(IpcProxy::SuspendForFork());
    if (ret == UBSM_OK) {
        ret = UbseMemExecutor::GetInstance().ForkPrepare();
    }
    if (ret != UBSM_OK) {
        // Resume also reconciles a lost suspend acknowledgement. Never reopen on failed rollback.
        const auto rollback = GetErrCode(IpcProxy::Resume());
        DBG_LOGERROR("Fork prepare failed, pid=" << ownerPid_.load() << ", generation=" << generation_.load()
                                                 << ", error=" << ret << ", rollback=" << rollback);
        if (rollback == UBSM_OK) {
            Open();
        } else {
            phase_.store(Phase::PREPARE_FAILED);
        }
        const auto failureResult = ret == UBSM_ERR_BUSY ? UBSM_ERR_MEMLIB : ret;
        return failureResult;
    }
    DBG_LOGINFO("Fork prepared, pid=" << ownerPid_.load() << ", generation=" << generation_.load());
    phase_.store(Phase::PREPARED);
    return UBSM_OK;
}

int ForkCoordinator::ResumeParent()
{
    if (!parentIpcReady_) {
        const auto ret = GetErrCode(IpcProxy::Resume());
        if (ret != UBSM_OK) {
            const auto failureResult = ret == UBSM_ERR_BUSY ? UBSM_ERR_MEMLIB : ret;
            if (failureResult != 0) {
                DBG_LOGERROR("Failed to restore parent IPC connection, error=" << ret);
            }
            return failureResult;
        }
        parentIpcReady_ = true;
    }
    const auto ret = UbseMemExecutor::GetInstance().ForkResume();
    if (ret != UBSM_OK) {
        DBG_LOGERROR("Failed to restore parent UBSE client, error=" << ret);
        return ret == UBSM_ERR_BUSY ? UBSM_ERR_MEMLIB : ret;
    }
    return UBSM_OK;
}

int ForkCoordinator::Parent()
{
    if (forkCallbackDepth != 0) {
        return UBSM_ERR_BUSY;
    }
    auto phase = phase_.load();
    if (phase == Phase::RUNNING) {
        ForkApiGuard confirmation;
        if (!confirmation.Entered()) {
            return UBSM_ERR_BUSY;
        }
        const auto pid = runtimePid_.load();
        const auto failureResult = pid == 0 ? UBSM_ERR_MEMLIB : (pid == getpid() ? UBSM_OK : UBSM_ERR_PARAM_INVALID);
        if (failureResult != 0) {
            DBG_LOGERROR("Parent confirmation rejected because the SDK is uninitialized or belongs to another process, "
                         "runtimePid="
                         << pid << ", pid=" << getpid());
        }
        return failureResult;
    }
    if (phase != Phase::PREPARED && phase != Phase::PREPARE_FAILED && phase != Phase::PARENT_FAILED) {
        const auto failureResult = phase == Phase::RECOVERING || phase == Phase::PREPARING ? UBSM_ERR_BUSY :
                                                                                             UBSM_ERR_PARAM_INVALID;
        return failureResult;
    }
    // Reject foreign callers without ever publishing a transient phase that fork could inherit.
    if (getpid() != ownerPid_.load() || preparedGeneration != generation_.load()) {
        return UBSM_ERR_PARAM_INVALID;
    }
    if (!phase_.compare_exchange_strong(phase, Phase::RECOVERING)) {
        return UBSM_ERR_BUSY;
    }
    const auto ret = ResumeParent();
    if (ret == UBSM_OK) {
        Open();
    } else {
        DBG_LOGERROR("Fork parent recovery failed, pid=" << getpid() << ", generation=" << generation_.load()
                                                         << ", error=" << ret);
        phase_.store(Phase::PARENT_FAILED);
    }
    return ret;
}

int ForkCoordinator::Child()
{
    if (forkCallbackDepth != 0) {
        return UBSM_ERR_BUSY;
    }
    auto phase = phase_.load();
    if (phase != Phase::PREPARED && phase != Phase::CHILD_FAILED) {
        const auto failureResult = phase == Phase::RECOVERING || phase == Phase::PREPARING ? UBSM_ERR_BUSY :
                                                                                             UBSM_ERR_PARAM_INVALID;
        return failureResult;
    }
    if (getpid() == ownerPid_.load() || preparedGeneration != generation_.load()) {
        return UBSM_ERR_PARAM_INVALID;
    }
    if (!phase_.compare_exchange_strong(phase, Phase::RECOVERING)) {
        return UBSM_ERR_BUSY;
    }
    if (!childReset_) {
        // Kernel already excluded every SDK mapping. No old close/munmap/free/unlock requests here.
        ShmMetaDataMgr::GetInstance().ForkChildResetNoLock();
        BorrowAppMetaMgr::GetInstance().ForkChildResetNoLock();
        UbseMemExecutor::GetInstance().ForkChildReset();
        runtimePid_.store(getpid());
        preparedGeneration = ++generation_;
        // Disable inherited callbacks before any child runtime can log or start threads.
        const auto loggerResult = ubsmem::log::UbsmemLoggerManager::ForkChildReset();
        if (loggerResult != 0) {
            phase_.store(Phase::CHILD_FAILED);
            const auto failureResult = loggerResult == -2 ? UBSM_ERR_NOT_SUPPORTED : UBSM_ERR_MALLOC_FAIL;
            if (failureResult != 0) {
                DBG_LOGERROR("Failed to rebuild child logger, error=" << loggerResult);
            }
            return failureResult;
        }
        childReset_ = true;
    }
    auto ret = GetErrCode(IpcProxy::ForkChildInitialize());
    if (ret == UBSM_OK) {
        ret = UbseMemExecutor::GetInstance().ForkResume();
    }
    if (ret == UBSM_OK) {
        Open();
    } else {
        DBG_LOGERROR("Fork child recovery failed, pid=" << getpid() << ", generation=" << generation_.load()
                                                        << ", error=" << ret);
        phase_.store(Phase::CHILD_FAILED);
    }
    const auto failureResult = ret == UBSM_ERR_BUSY ? UBSM_ERR_MEMLIB : ret;
    return failureResult;
}
} // namespace ock::mxmd

extern "C" {
int ubsmem_fork_prepare(void)
{
    return ock::mxmd::ForkCoordinator::Instance().Prepare();
}
int ubsmem_fork_parent(void)
{
    return ock::mxmd::ForkCoordinator::Instance().Parent();
}
int ubsmem_fork_child(void)
{
    return ock::mxmd::ForkCoordinator::Instance().Child();
}
}
