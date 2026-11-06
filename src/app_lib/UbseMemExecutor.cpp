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
#include "UbseMemExecutor.h"
#include "defines.h"
#include "dlfcn.h"
#include "fork_callback_context.h"
#include "log.h"
#include "rack_mem_err.h"
#include "system_adapter.h"
namespace ock::mxmd {
using namespace ock::common;
using namespace ock::ubsm;
std::atomic<ShmFaultFuncPtr> UbseMemExecutor::ShmFaultFunc{nullptr};
static void ForkAwareEngineLogger(uint32_t level, const char *message)
{
    ForkCallbackScope callback(true);
    const auto logger = ubsmem::log::UbsmemLoggerManager::Instance()->GetExternLogCallback();
    if (logger != nullptr) {
        logger(static_cast<int>(level), message);
    }
}
uint32_t UbseMemExecutor::Initialize()
{
    if (initialized_) {
        return HOK;
    }
    static constexpr auto soPath = ARCH_LIB_DIR "/libubse-client.so.1";
    handle = SystemAdapter::DlOpen(soPath, RTLD_NOW);
    if (handle == nullptr) {
        DBG_LOGERROR("Dlopen libubse-client.so.1 failed, error is " << dlerror());
        return MXM_ERR_UBSE_LIB;
    }
    UbseLogCallBackRegisterFunc =
        (UbseLogCallBackRegisterPtr)SystemAdapter::DlSym(handle, "ubs_engine_log_callback_register");
    if (UbseLogCallBackRegisterFunc == nullptr) {
        DBG_LOGWARN("Symbol ubse_log_callback_register is not found , reason is" << dlerror());
    } else {
        UbseLogCallBackRegisterFunc(ForkAwareEngineLogger);
    }

    UbseFaultEventRegisterFunc = (UbseFaultEventRegisterPtr)SystemAdapter::DlSym(handle, "ubs_mem_shm_fault_register");
    if (UbseFaultEventRegisterFunc == nullptr) {
        DBG_LOGERROR("Failed to call dlsym to load ubs_mem_shm_fault_register, error is " << dlerror());
        SystemAdapter::DlClose(handle);
        handle = nullptr;
        return MXM_ERR_UBSE_LIB;
    }
    clientInitialize_ =
        reinterpret_cast<int32_t (*)(const char *)>(SystemAdapter::DlSym(handle, "ubs_engine_client_initialize"));
    clientFinalize_ = reinterpret_cast<void (*)()>(SystemAdapter::DlSym(handle, "ubs_engine_client_finalize"));
    initialized_ = true;
    return HOK;
}

uint32_t UbseMemExecutor::Exit()
{
    if (handle != nullptr) {
        if (clientFinalize_ != nullptr && (!restartAfterFork_ || clientRestarted_)) {
            clientFinalize_();
        }
        SystemAdapter::DlClose(handle);
        handle = nullptr;
        UbseFaultEventRegisterFunc = nullptr;
        clientInitialize_ = nullptr;
        clientFinalize_ = nullptr;
    }
    initialized_ = false;
    restartAfterFork_ = false;
    clientRestarted_ = false;
    ShmFaultFunc = nullptr;
    return HOK;
}

int32_t UbseMemExecutor::UbseShmFaultHandlerFunc(const char *name, uint64_t memid, ubs_mem_fault_type_t type)
{
    const auto handler = ShmFaultFunc.load();
    if (name == nullptr || handler == nullptr) {
        DBG_LOGERROR("name or registerFunc is nullptr");
        return MXM_ERR_PARAM_INVALID;
    }
    ForkCallbackScope callback;
    return handler(name, static_cast<ubsmem_fault_type_t>(type));
}

int32_t UbseMemExecutor::RegisterShmFaultEvent(ShmFaultFuncPtr registerFunc)
{
    if (!initialized_) {
        DBG_LOGERROR("UbseMemExecutor not initialized");
        return -1;
    }
    ShmFaultFunc = registerFunc;
    auto ret = UbseFaultEventRegisterFunc(UbseShmFaultHandlerFunc);
    if (ret != UBS_SUCCESS) {
        DBG_LOGERROR("register shm fault event failed ret is " << ret);
        return MXM_ERR_UBSE_INNER;
    }
    return HOK;
}
int UbseMemExecutor::ForkPrepare()
{
    if (!initialized_) {
        return UBSM_OK;
    }
    if (clientInitialize_ == nullptr || clientFinalize_ == nullptr) {
        DBG_LOGERROR("UBSE fork lifecycle symbols are unavailable, hasInitialize="
                     << (clientInitialize_ != nullptr) << ", hasFinalize=" << (clientFinalize_ != nullptr));
        return UBSM_ERR_NOT_SUPPORTED;
    }
    if (!restartAfterFork_) {
        // Finalize returns after the UBSE client has stopped.
        clientFinalize_();
        restartAfterFork_ = true;
        clientRestarted_ = false;
    }
    return UBSM_OK;
}

void UbseMemExecutor::ForkChildReset()
{
    ShmFaultFunc.store(nullptr);
    // Do not carry a parent subscription into child restart, reconnect, or a later fork.
    clientRestarted_ = false;
}

int UbseMemExecutor::ForkResume()
{
    if (!restartAfterFork_) {
        return UBSM_OK;
    }
    if (!clientRestarted_) {
        const auto initializeRet = clientInitialize_(nullptr);
        if (initializeRet != UBS_SUCCESS) {
            clientFinalize_();
            DBG_LOGERROR("Failed to restart UBSE client after fork, error=" << initializeRet);
            return UBSM_ERR_UBSE;
        }
        clientRestarted_ = true;
        if (UbseLogCallBackRegisterFunc != nullptr) {
            UbseLogCallBackRegisterFunc(ForkAwareEngineLogger);
        }
    }
    if (ShmFaultFunc != nullptr) {
        const auto registerRet = UbseFaultEventRegisterFunc(UbseShmFaultHandlerFunc);
        if (registerRet != UBS_SUCCESS) {
            DBG_LOGERROR("Failed to restore UBSE fault subscription after fork, error=" << registerRet);
            return UBSM_ERR_UBSE;
        }
    }
    restartAfterFork_ = false;
    return UBSM_OK;
}
} // namespace ock::mxmd
