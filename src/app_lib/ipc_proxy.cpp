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

#include "ipc_proxy.h"
#include <dlfcn.h>
#include "shm_ipc_command.h"
#include "ubsm_check_resource.h"

using namespace ock::dagger;

namespace ock::mxmd {
using namespace ock::common;
ReadWriteLock IpcProxy::stateLock_;
IpcSuspendState IpcProxy::state_ = IpcSuspendState::UNINITIALIZED;

static bool IsTransportError(uint32_t ret)
{
    return ret == MXM_ERR_IPC_INIT_CALL || ret == MXM_ERR_IPC_SYNC_CALL || ret == MXM_ERR_IPC_HCOM_INNER_SYNC_CALL ||
           ret == MXM_ERR_IPC_CRC_CHECK_ERROR || ret == MXM_ERR_IPC_SERIALIZE_DESERIALIZE_ERROR;
}

uint32_t IpcProxy::GetOperationStateError()
{
    switch (state_) {
        case IpcSuspendState::RUNNING:
        case IpcSuspendState::UNINITIALIZED:
            return UBSM_OK;
        case IpcSuspendState::SUSPENDED:
            DBG_LOGERROR("IPC control operation rejected because the client is suspended.");
            return MXM_ERR_IPC_SUSPENDED;
        case IpcSuspendState::SUSPENDING:
        case IpcSuspendState::RESUMING:
        case IpcSuspendState::FINALIZING:
            DBG_LOGERROR(
                "IPC control operation rejected during lifecycle transition, state=" << static_cast<int>(state_));
            return MXM_ERR_NAME_BUSY;
        case IpcSuspendState::FAILED_SUSPENDING:
        case IpcSuspendState::FAILED_STOPPING:
        case IpcSuspendState::FAILED_RESUMING:
            DBG_LOGERROR("IPC control operation rejected because the client lifecycle is in failed state.");
            return MXM_ERR_MEMLIB;
    }
    DBG_LOGERROR("IPC control operation rejected because the client state is invalid.");
    return MXM_ERR_MEMLIB;
}

uint32_t IpcProxy::Initialize()
{
    WriteLocker<ReadWriteLock> stateGuard(&stateLock_);
    if (state_ == IpcSuspendState::RUNNING) {
        return UBSM_OK;
    }
    if (state_ != IpcSuspendState::UNINITIALIZED) {
        DBG_LOGERROR("Failed to initialize IPC in state=" << static_cast<int>(state_));
        return MXM_ERR_NAME_BUSY;
    }
    const auto start = Monotonic::TimeUs();
    auto ret = MxmComStartIpcClient();
    if (ret != 0) {
        const auto end = Monotonic::TimeUs();
        DBG_LOGERROR("Failed to init ipc, ret=" << ret << ", cost time=" << (end - start) << "us");
        return MXM_ERR_IPC_INIT_CALL;
    }
    ret = MxmSetPostReconnectHandler(ock::ubsm::UbsmCheckResource::UbsmCheckResourceHandler);
    if (ret != 0) {
        DBG_LOGERROR("MxmSetPostReconnectHandler failed. ret=" << ret);
        MxmComStopIpcClient();
        return MXM_ERR_IPC_INIT_CALL;
    }
    state_ = IpcSuspendState::RUNNING;
    return UBSM_OK;
}

uint32_t IpcProxy::Suspend()
{
    IpcSuspendState entryState;
    {
        WriteLocker<ReadWriteLock> stateGuard(&stateLock_);
        entryState = state_;
        switch (entryState) {
            case IpcSuspendState::SUSPENDED:
                return UBSM_OK;
            case IpcSuspendState::UNINITIALIZED:
                DBG_LOGERROR("Failed to suspend an uninitialized IPC client.");
                return MXM_ERR_MEMLIB;
            case IpcSuspendState::RUNNING:
            case IpcSuspendState::FAILED_SUSPENDING:
            case IpcSuspendState::FAILED_STOPPING:
                state_ = IpcSuspendState::SUSPENDING;
                break;
            default:
                DBG_LOGERROR("Failed to suspend IPC in state=" << static_cast<int>(entryState));
                return MXM_ERR_NAME_BUSY;
        }
    }

    if (entryState != IpcSuspendState::FAILED_STOPPING) {
        auto ret = ShmIpcCommand::IpcCallSuspendInner();
        if (ret != UBSM_OK) {
            WriteLocker<ReadWriteLock> stateGuard(&stateLock_);
            const bool firstRequestRejected = entryState == IpcSuspendState::RUNNING && !IsTransportError(ret);
            state_ = firstRequestRejected ? IpcSuspendState::RUNNING : IpcSuspendState::FAILED_SUSPENDING;
            return ret;
        }
    }

    auto ret = MxmComStopIpcClient();
    {
        WriteLocker<ReadWriteLock> stateGuard(&stateLock_);
        if (ret != UBSM_OK) {
            state_ = IpcSuspendState::FAILED_STOPPING;
            DBG_LOGERROR("Failed to stop IPC client after suspend acknowledgement, ret=" << ret);
            return MXM_ERR_MEMLIB;
        }
        state_ = IpcSuspendState::SUSPENDED;
    }
    return UBSM_OK;
}

uint32_t IpcProxy::Resume()
{
    IpcSuspendState stateBeforeResume;
    auto ret = EnterResumingState(stateBeforeResume);
    if (ret != UBSM_OK || stateBeforeResume == IpcSuspendState::RUNNING) {
        return ret;
    }

    ret = EnsureResumeClientReady(stateBeforeResume);
    if (ret != UBSM_OK) {
        return ret;
    }

    return HandleResumeResult(stateBeforeResume, ShmIpcCommand::IpcCallResumeInner());
}

uint32_t IpcProxy::EnterResumingState(IpcSuspendState &stateBeforeResume)
{
    WriteLocker<ReadWriteLock> stateGuard(&stateLock_);
    stateBeforeResume = state_;
    switch (stateBeforeResume) {
        case IpcSuspendState::RUNNING:
            return UBSM_OK;
        case IpcSuspendState::UNINITIALIZED:
            DBG_LOGERROR("Failed to resume an uninitialized IPC client.");
            return MXM_ERR_MEMLIB;
        case IpcSuspendState::SUSPENDED:
        case IpcSuspendState::FAILED_SUSPENDING:
        case IpcSuspendState::FAILED_STOPPING:
        case IpcSuspendState::FAILED_RESUMING:
            state_ = IpcSuspendState::RESUMING;
            return UBSM_OK;
        default:
            DBG_LOGERROR("Failed to resume IPC in state=" << static_cast<int>(stateBeforeResume));
            return MXM_ERR_NAME_BUSY;
    }
}

uint32_t IpcProxy::EnsureResumeClientReady(IpcSuspendState stateBeforeResume)
{
    const bool clientAvailable = stateBeforeResume == IpcSuspendState::FAILED_SUSPENDING ||
                                 stateBeforeResume == IpcSuspendState::FAILED_RESUMING;
    if (clientAvailable) {
        return UBSM_OK;
    }

    if (stateBeforeResume == IpcSuspendState::FAILED_STOPPING) {
        auto ret = MxmComStopIpcClient();
        if (ret != UBSM_OK) {
            SetState(IpcSuspendState::FAILED_STOPPING);
            return MXM_ERR_MEMLIB;
        }
    }

    auto ret = MxmComStartIpcClient();
    if (ret != UBSM_OK) {
        SetState(IpcSuspendState::SUSPENDED);
        DBG_LOGERROR("Failed to start IPC client while resuming, ret=" << ret);
        return MXM_ERR_IPC_INIT_CALL;
    }
    ret = MxmSetPostReconnectHandler(ock::ubsm::UbsmCheckResource::UbsmCheckResourceHandler);
    if (ret != UBSM_OK) {
        DBG_LOGERROR("Failed to set reconnect handler while resuming, ret=" << ret);
        const auto stopRet = MxmComStopIpcClient();
        SetState(stopRet == UBSM_OK ? IpcSuspendState::SUSPENDED : IpcSuspendState::FAILED_STOPPING);
        return MXM_ERR_IPC_INIT_CALL;
    }
    return UBSM_OK;
}

uint32_t IpcProxy::HandleResumeResult(IpcSuspendState stateBeforeResume, uint32_t ret)
{
    if (ret == UBSM_OK) {
        SetState(IpcSuspendState::RUNNING);
        return UBSM_OK;
    }

    const bool requestRejected = !IsTransportError(ret);
    const bool suspendStateUncertain = stateBeforeResume == IpcSuspendState::FAILED_SUSPENDING;
    if (requestRejected && !suspendStateUncertain) {
        const auto stopRet = MxmComStopIpcClient();
        if (stopRet != UBSM_OK) {
            SetState(IpcSuspendState::FAILED_STOPPING);
            DBG_LOGERROR("Failed to stop IPC client after resume failure, ret=" << stopRet);
            return ret;
        }
    }

    if (IsTransportError(ret)) {
        SetState(IpcSuspendState::FAILED_RESUMING);
    } else {
        SetState(suspendStateUncertain ? IpcSuspendState::FAILED_SUSPENDING : IpcSuspendState::SUSPENDED);
    }
    return ret;
}

void IpcProxy::SetState(IpcSuspendState state)
{
    WriteLocker<ReadWriteLock> stateGuard(&stateLock_);
    state_ = state;
}

uint32_t IpcProxy::Destroy()
{
    {
        WriteLocker<ReadWriteLock> stateGuard(&stateLock_);
        if (state_ == IpcSuspendState::UNINITIALIZED) {
            return UBSM_OK;
        }
        if (state_ == IpcSuspendState::SUSPENDING || state_ == IpcSuspendState::RESUMING ||
            state_ == IpcSuspendState::FINALIZING) {
            DBG_LOGERROR("Failed to destroy IPC during lifecycle transition, state=" << static_cast<int>(state_));
            return MXM_ERR_NAME_BUSY;
        }
        state_ = IpcSuspendState::FINALIZING;
    }
    auto ret = MxmComStopIpcClient();
    {
        WriteLocker<ReadWriteLock> stateGuard(&stateLock_);
        state_ = ret == UBSM_OK ? IpcSuspendState::UNINITIALIZED : IpcSuspendState::FAILED_STOPPING;
    }
    if (ret != UBSM_OK) {
        DBG_LOGERROR("Failed to stop IPC client while destroying, ret=" << ret);
    }
    return ret == UBSM_OK ? static_cast<uint32_t>(UBSM_OK) : static_cast<uint32_t>(MXM_ERR_MEMLIB);
}

uint32_t IpcProxy::SyncCall(int opcode, MsgBase *request, MsgBase *response)
{
    auto ret = MxmComIpcClientSend(opcode, request, response);
    if (ret != 0) {
        return MXM_ERR_IPC_HCOM_INNER_SYNC_CALL;
    }
    return UBSM_OK;
}
} // namespace ock::mxmd
