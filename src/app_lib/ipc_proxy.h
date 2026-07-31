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

#ifndef MEMORYFABRIC_IPC_PROXY_H
#define MEMORYFABRIC_IPC_PROXY_H

#include <functional>
#include <string>
#include <utility>

#include "lock/dg_lock.h"
#include "log.h"
#include "mxm_ipc_client_interface.h"
#include "mxm_msg.h"
#include "ptracer.h"
#include "rack_mem_err.h"
#include "ubsm_ptracer.h"

namespace ock::mxmd {
using namespace ock::common;

enum class IpcSuspendState {
    UNINITIALIZED,
    RUNNING,
    SUSPENDING,
    SUSPENDED,
    RESUMING,
    FINALIZING,
    FAILED_SUSPENDING, // Suspend请求结果不确定，IPC client仍保留
    FAILED_STOPPING,   // 本地IPC client停止失败，client/engine保留等待重试
    FAILED_RESUMING,   // Resume请求发生传输错误，已启动的IPC client仍保留
};

class IpcProxy {
public:
    static uint32_t Initialize();

    static uint32_t Suspend();

    static uint32_t Resume();

    uint32_t SyncCall(int opcode, MsgBase *request, MsgBase *response);

    static uint32_t Destroy();

    template <typename Operation>
    static uint32_t RunOperation(Operation &&operation)
    {
        ock::dagger::ReadLocker<ock::dagger::ReadWriteLock> stateGuard(&stateLock_);
        const auto ret = GetOperationStateError();
        if (ret != UBSM_OK) {
            return ret;
        }
        return operation();
    }

    uint32_t Ipc(MsgBase *request, MsgBase *response, const int opCode)
    {
        if (request == nullptr) {
            DBG_LOGERROR("IPC request is nullptr.");
            return MXM_ERR_PARAM_INVALID;
        }
        if (response == nullptr) {
            DBG_LOGERROR("IPC response is nullptr.");
            return MXM_ERR_PARAM_INVALID;
        };

        return RunOperation([&] { return IpcCallInner(*request, *response, opCode); });
    }

    uint32_t IpcCallInner(MsgBase &request, MsgBase &response, const int opCode)
    {
        DBG_LOGINFO("IPC SyncCall begin, opCode=" << opCode);
        uint32_t hr;
        for (int i = 0; i < 3u; ++i) {
            TP_TRACE_BEGIN(TP_UBSM_IPC_CALL);
            hr = SyncCall(opCode, &request, &response);
            TP_TRACE_END(TP_UBSM_IPC_CALL, hr);
            if (hr != MXM_ERR_IPC_CRC_CHECK_ERROR && hr != MXM_ERR_IPC_SERIALIZE_DESERIALIZE_ERROR) {
                break;
            }
            DBG_LOGWARN("Failed to send message, ret=" << hr << ", opCode=" << opCode << ", retring");
            usleep(300U * (i + 1U) * (i + 1U));
        }
        if (BresultFail(hr)) {
            DBG_LOGERROR("Failed to send message, ret=" << hr << " ,opCode" << opCode);
            return hr;
        }
        DBG_LOGINFO("IPC SyncCall end, hr=" << hr);
        return hr;
    }

    static IpcProxy &GetInstance()
    {
        static IpcProxy instance;
        return instance;
    }
    IpcProxy(const IpcProxy &other) = default;
    IpcProxy(IpcProxy &&other) = default;
    IpcProxy &operator=(const IpcProxy &other) = default;
    IpcProxy &operator=(IpcProxy &&other) noexcept = default;

    DAGGER_DEFINE_REF_COUNT_FUNCTIONS
private:
    static uint32_t GetOperationStateError();
    static ock::dagger::ReadWriteLock stateLock_;
    static IpcSuspendState state_;
    DAGGER_DEFINE_REF_COUNT_VARIABLE;
    IpcProxy() = default;
};
} // namespace ock::mxmd

#endif // MEMORYFABRIC_IPC_PROXY_H
