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

#include "mxm_rpc_server_interface.h"
#include "mxm_message_handler.h"
#include "mxm_rpc_server.h"
#include "rpc_config.h"
#include "util/defines.h"

#include <condition_variable>
#include <mutex>

namespace ock::com::rpc {
MxmRpcServer *g_mxmRpcServer{nullptr};
int g_rpcServerCount{0};
std::mutex g_rpcServerMutex;
std::condition_variable g_rpcServerCv;
uint32_t g_activeRpcOperations{0};
bool g_rpcServerStopping{false};

const std::string RACK_AGENT_RPC_SERVER_ENGINE_NAME = "MxmAgentRpcServerEngine";

HRESULT RegRpcService(MxmComBaseMessageHandlerPtr &handlerPtr)
{
    std::unique_lock<std::mutex> lock(g_rpcServerMutex);
    if (g_mxmRpcServer == nullptr || g_rpcServerStopping) {
        return HFAIL;
    }
    auto server = g_mxmRpcServer;
    ++g_activeRpcOperations;
    lock.unlock();
    auto ret = server->RegMessageHandler(handlerPtr);
    lock.lock();
    --g_activeRpcOperations;
    lock.unlock();
    g_rpcServerCv.notify_all();
    return ret;
}

/**
 * @brief 启动Rpc客户端
 * @return 成功返回0, 失败返回错误码
 */
int MxmComStartRpcServer()
{
    std::lock_guard<std::mutex> lock(g_rpcServerMutex);
    if (g_rpcServerStopping) {
        DBG_LOGERROR("RPC server is stopping.");
        return HFAIL;
    }
    if (g_mxmRpcServer != nullptr) {
        ++g_rpcServerCount;
        return HOK;
    }

    auto ipAndPort = NetRpcConfig::GetInstance().GetLocalNode();
    DBG_LOGINFO("Start Rpc Server, local node name=" << ipAndPort.name << ", ip=" << ipAndPort.ip);
    auto rpcServer = new (std::nothrow) MxmRpcServer(RACK_AGENT_RPC_SERVER_ENGINE_NAME, ipAndPort);
    if (rpcServer == nullptr) {
        DBG_LOGERROR("rpcServer is nullptr.");
        return HFAIL;
    }

    auto ret = rpcServer->Start();
    if (ret != HOK) {
        DBG_LOGERROR("Start MxmRpcServer fail," << ret);
        rpcServer->Stop();
        delete rpcServer;
        rpcServer = nullptr;
        return HFAIL;
    }
    g_mxmRpcServer = rpcServer;
    ++g_rpcServerCount;
    return HOK;
}

/**
 * @brief 停止Rpc客户端
 */
void MxmComStopRpcServer()
{
    std::unique_lock<std::mutex> lock(g_rpcServerMutex);
    if (g_rpcServerCount == 0 || g_mxmRpcServer == nullptr) {
        DBG_LOGINFO("Server has been stopped");
        return;
    }
    if (--g_rpcServerCount > 0) {
        return;
    }
    g_rpcServerStopping = true;
    g_rpcServerCv.wait(lock, [] { return g_activeRpcOperations == 0; });
    auto server = g_mxmRpcServer;
    lock.unlock();
    server->Stop();
    lock.lock();
    g_mxmRpcServer = nullptr;
    g_rpcServerStopping = false;
    lock.unlock();
    delete server;
    g_rpcServerCv.notify_all();
}

uint32_t MxmRegRpcService(const MxmComEndpoint &endpoint, const MxmComServiceHandler &handler)
{
    uint16_t moduleCode = endpoint.moduleId;
    auto opCode = static_cast<uint16_t>(endpoint.serviceId);
    MxmComBaseMessageHandlerPtr handlerPtr = new (std::nothrow) MxmNetMessageHandler(opCode, moduleCode, handler);
    if (handlerPtr == nullptr) {
        DBG_LOGERROR("New MxmComBaseMessageHandler fail");
        return HFAIL;
    }
    auto ret = HOK;

    ret = RegRpcService(handlerPtr);
    if (ret != HOK) {
        DBG_LOGERROR("Rpc Service fail, ret " << ret);
        return ret;
    }

    DBG_LOGINFO("serviceId is " << opCode << " ret is " << ret);
    return ret;
}

int MxmComRpcServerSend(uint16_t opCode, MsgBase *request, MsgBase *response, const std::string &nodeId)
{
    SendParam param(nodeId, static_cast<uint16_t>(MxmModuleCode::MEM), opCode, MxmChannelType::NORMAL);
    std::unique_lock<std::mutex> lock(g_rpcServerMutex);
    if (g_mxmRpcServer == nullptr || g_rpcServerStopping) {
        DBG_LOGERROR("g_mxmRpcServer is nullptr");
        return HFAIL;
    }
    if (request == nullptr || response == nullptr) {
        DBG_LOGERROR("request or response is nullptr");
        return HFAIL;
    }
    auto server = g_mxmRpcServer;
    ++g_activeRpcOperations;
    lock.unlock();
    auto ret = server->Send(param, request, response);
    lock.lock();
    --g_activeRpcOperations;
    lock.unlock();
    g_rpcServerCv.notify_all();
    return ret;
}

int MxmComRpcServerConnect(const RpcNode &nodeId)
{
    std::unique_lock<std::mutex> lock(g_rpcServerMutex);
    if (g_mxmRpcServer == nullptr || g_rpcServerStopping) {
        DBG_LOGERROR("g_mxmRpcServer is nullptr");
        return HFAIL;
    }
    auto server = g_mxmRpcServer;
    ++g_activeRpcOperations;
    lock.unlock();
    auto ret = server->Connect(nodeId);
    lock.lock();
    --g_activeRpcOperations;
    lock.unlock();
    g_rpcServerCv.notify_all();
    return ret;
}
} // namespace ock::com::rpc
