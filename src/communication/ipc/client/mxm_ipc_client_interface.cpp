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

#include "mxm_ipc_client_interface.h"

#include <condition_variable>
#include <mutex>
#include <utility>
#include "mxm_com_error.h"
#include "mxm_ipc_client.h"
#include "mxm_msg.h"
#include "ubsm_com_constants.h"

using namespace ock::com;
using namespace ock::common;
using namespace ock::mxmd;
using namespace ock::com::ipc;
MxmIpcClient *g_mxmIpcClient{nullptr};
std::atomic<int> g_ipcClientCount{0};
static std::mutex g_ipcClientMutex;
static std::condition_variable g_ipcClientCv;
static uint32_t g_activeIpcSends{0};
static bool g_ipcClientStopping{false};
static bool g_ipcClientStopInProgress{false};

int MxmComStartIpcClient()
{
    std::lock_guard<std::mutex> lock(g_ipcClientMutex);
    if (g_ipcClientStopping) {
        DBG_LOGERROR("IPC client is stopping.");
        return HFAIL;
    }
    if (g_ipcClientCount.load() > 0 && g_mxmIpcClient != nullptr) {
        g_ipcClientCount.fetch_add(1);
        return HOK;
    }
    std::string udsPathPrefix = MXM_IPC_UDS_PATH_PREFIX_DEFAULT;
    int result = setenv("HCOM_FILE_PATH_PREFIX", udsPathPrefix.c_str(), 1);
    if (result != 0) {
        DBG_LOGERROR("Failed to set environment HCOM_FILE_PATH, ret=" << result);
        return HFAIL;
    }

    auto client = new (std::nothrow)
        MxmIpcClient(MXM_AGENT_IPC_SERVER_NAME, MXM_AGENT_IPC_CLIENT_ENGINE_NAME, FAKE_CUR_NODE_ID);
    if (client == nullptr) {
        DBG_LOGERROR("no memory");
        return MXM_COM_ERROR_NEW_IPC_CLIENT_FAIL;
    }
    auto ret = client->Start();
    if (ret != HOK) {
        DBG_LOGERROR("Failed to start client, ret=" << ret);
        delete client;
        return ret;
    }
    ret = client->Connect();
    if (ret != HOK) {
        DBG_LOGERROR("Failed to connect other, ret=" << ret);
        client->Stop();
        delete client;
        return ret;
    }

    g_mxmIpcClient = client;
    g_ipcClientCount.fetch_add(1);
    return HOK;
}

int MxmComStopIpcClient()
{
    std::unique_lock<std::mutex> lock(g_ipcClientMutex);
    g_ipcClientCv.wait(lock, [] { return !g_ipcClientStopInProgress; });
    if (g_ipcClientCount.load() > 1) {
        g_ipcClientCount.fetch_sub(1);
        return HOK;
    }
    if (g_mxmIpcClient == nullptr || g_ipcClientCount.load() != 1) {
        return HOK;
    }

    g_ipcClientStopping = true;
    g_ipcClientStopInProgress = true;
    g_ipcClientCv.wait(lock, [] { return g_activeIpcSends == 0; });
    auto client = g_mxmIpcClient;
    lock.unlock();

    auto ret = client->Stop();

    lock.lock();
    g_ipcClientStopInProgress = false;
    if (ret != HOK) {
        lock.unlock();
        g_ipcClientCv.notify_all();
        DBG_LOGERROR("Failed to stop IPC client, ret=" << ret);
        return ret;
    }
    g_mxmIpcClient = nullptr;
    g_ipcClientCount.store(0);
    g_ipcClientStopping = false;
    lock.unlock();
    delete client;
    g_ipcClientCv.notify_all();
    return ret;
}

int MxmComIpcClientSend(uint16_t opCode, MsgBase *request, MsgBase *response)
{
    SendParam param(FAKE_CUR_NODE_ID, static_cast<uint16_t>(MxmModuleCode::MEM), opCode, MxmChannelType::SINGLE_SIDE);
    std::unique_lock<std::mutex> lock(g_ipcClientMutex);
    if (g_mxmIpcClient == nullptr || g_ipcClientStopping) {
        DBG_LOGERROR("g_mxmIpcClient is nullptr");
        return HFAIL;
    }
    auto client = g_mxmIpcClient;
    ++g_activeIpcSends;
    lock.unlock();

    auto ret = client->Send(param, request, response);

    lock.lock();
    --g_activeIpcSends;
    const bool notifyStop = g_activeIpcSends == 0;
    lock.unlock();
    if (notifyStop) {
        g_ipcClientCv.notify_all();
    }
    return ret;
}

int MxmSetPostReconnectHandler(int (*handler)())
{
    std::lock_guard<std::mutex> lock(g_ipcClientMutex);
    if (g_mxmIpcClient == nullptr || g_ipcClientStopping) {
        DBG_LOGERROR("g_mxmIpcClient is nullptr");
        return HFAIL;
    }

    return g_mxmIpcClient->SetPostReconnectHandler(handler);
}
