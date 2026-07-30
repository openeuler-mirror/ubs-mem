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
std::recursive_mutex IpcProxy::stateLock_;
IpcSuspendState IpcProxy::state_ = IpcSuspendState::UNINITIALIZED;

static bool IsTransportError(uint32_t ret)
{
    return ret == MXM_ERR_IPC_INIT_CALL || ret == MXM_ERR_IPC_SYNC_CALL || ret == MXM_ERR_IPC_HCOM_INNER_SYNC_CALL ||
           ret == MXM_ERR_IPC_CRC_CHECK_ERROR || ret == MXM_ERR_IPC_SERIALIZE_DESERIALIZE_ERROR;
}

uint32_t IpcProxy::Initialize()
{
    std::lock_guard<std::recursive_mutex> stateGuard(stateLock_);
    if (state_ == IpcSuspendState::RUNNING) {
        return UBSM_OK;
    }
    if (state_ != IpcSuspendState::UNINITIALIZED) {
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
    std::unique_lock<std::recursive_mutex> stateGuard(stateLock_);
    if (state_ == IpcSuspendState::SUSPENDED) {
        return UBSM_OK;
    }
    if (state_ == IpcSuspendState::UNINITIALIZED) {
        return MXM_ERR_MEMLIB;
    }
    if (state_ != IpcSuspendState::RUNNING) {
        return MXM_ERR_NAME_BUSY;
    }

    state_ = IpcSuspendState::SUSPENDING;
    stateGuard.unlock();
    auto ret = ShmIpcCommand::IpcCallSuspendUnlocked();
    if (ret != UBSM_OK) {
        stateGuard.lock();
        state_ = IsTransportError(ret) ? IpcSuspendState::FAILED : IpcSuspendState::RUNNING;
        return ret;
    }
    ret = MxmComStopIpcClient();
    stateGuard.lock();
    if (ret != UBSM_OK) {
        state_ = IpcSuspendState::FAILED;
        return MXM_ERR_MEMLIB;
    }
    state_ = IpcSuspendState::SUSPENDED;
    return UBSM_OK;
}

uint32_t IpcProxy::Resume()
{
    std::unique_lock<std::recursive_mutex> stateGuard(stateLock_);
    if (state_ == IpcSuspendState::RUNNING) {
        return UBSM_OK;
    }
    if (state_ == IpcSuspendState::UNINITIALIZED) {
        return MXM_ERR_MEMLIB;
    }
    if (state_ != IpcSuspendState::SUSPENDED) {
        return MXM_ERR_NAME_BUSY;
    }

    state_ = IpcSuspendState::RESUMING;
    stateGuard.unlock();
    auto ret = MxmComStartIpcClient();
    if (ret != UBSM_OK) {
        stateGuard.lock();
        state_ = IpcSuspendState::SUSPENDED;
        return MXM_ERR_IPC_INIT_CALL;
    }
    ret = MxmSetPostReconnectHandler(ock::ubsm::UbsmCheckResource::UbsmCheckResourceHandler);
    if (ret != UBSM_OK) {
        MxmComStopIpcClient();
        stateGuard.lock();
        state_ = IpcSuspendState::SUSPENDED;
        return MXM_ERR_IPC_INIT_CALL;
    }
    ret = ShmIpcCommand::IpcCallResumeUnlocked();
    if (ret != UBSM_OK) {
        if (!IsTransportError(ret)) {
            MxmComStopIpcClient();
        }
        stateGuard.lock();
        state_ = IsTransportError(ret) ? IpcSuspendState::FAILED : IpcSuspendState::SUSPENDED;
        return ret;
    }
    stateGuard.lock();
    state_ = IpcSuspendState::RUNNING;
    return UBSM_OK;
}

uint32_t IpcProxy::Destroy()
{
    std::unique_lock<std::recursive_mutex> stateGuard(stateLock_);
    if (state_ == IpcSuspendState::UNINITIALIZED) {
        return UBSM_OK;
    }
    if (state_ != IpcSuspendState::RUNNING) {
        return MXM_ERR_NAME_BUSY;
    }
    state_ = IpcSuspendState::FINALIZING;
    stateGuard.unlock();
    auto ret = MxmComStopIpcClient();
    stateGuard.lock();
    state_ = ret == UBSM_OK ? IpcSuspendState::UNINITIALIZED : IpcSuspendState::FAILED;
    return ret == UBSM_OK ? static_cast<uint32_t>(UBSM_OK) : static_cast<uint32_t>(MXM_ERR_MEMLIB);
}

bool IpcProxy::IsRunning()
{
    std::lock_guard<std::recursive_mutex> stateGuard(stateLock_);
    return state_ == IpcSuspendState::RUNNING;
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
