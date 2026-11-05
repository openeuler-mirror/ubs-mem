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
#include "mxm_com_engine.h"
#include <fcntl.h>
#include <grp.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include "crc/dg_crc.h"
#include "log.h"
#include "mxm_com_callback_slots.h"
#include "ptracer.h"
#include "ubs_common_config.h"
#include "ubsm_com_constants.h"
#include "ubsm_ptracer.h"

namespace ock::com {
using namespace ock::common;
using namespace ock::ubsm;

static void ReapReplies(ubs_hcom_service service);

static MxmComMessage *GetMessageFromNetServiceContext(ubs_hcom_service_context context)
{
    return static_cast<MxmComMessage *>(ubs_hcom_context_get_data(context));
}

static bool CheckMessageBodyLen(ubs_hcom_service_context context, MxmComMessage &msg)
{
    return ubs_hcom_context_get_datalen(context) == (sizeof(MxmComMessage) + msg.GetMessageBodyLen());
}

static uint64_t GetChannelIdFromNetServiceContext(ubs_hcom_service_context context)
{
    ubs_hcom_channel channel = 0;
    return ubs_hcom_context_get_channel(context, &channel) == HOK ? ubs_hcom_channel_get_id(channel) : 0;
}

static void GetUdsInfoFromNetServiceContext(ubs_hcom_service_context context, MxmUdsIdInfo &udsIdInfo)
{
    ubs_hcom_channel channel = 0;
    if (ubs_hcom_context_get_channel(context, &channel) != HOK) {
        return;
    }
    ubs_hcom_uds_id_info uds{};
    auto ret = ubs_hcom_channel_get_remote_uds_id_info(channel, &uds);
    if (ret == 0) {
        DBG_LOGDEBUG("Uds info is " << uds.pid << "," << uds.uid << "," << uds.gid);
        udsIdInfo.pid = uds.pid;
        udsIdInfo.uid = uds.uid;
        udsIdInfo.gid = uds.gid;
    }
}

static std::string GetUdsPath(const std::string &udsName)
{
    std::string udsPath = MXM_IPC_UDS_PATH_PREFIX_DEFAULT + udsName;
    return udsPath;
}

static void Logger(int level, const char *str)
{
    switch (level) {
        case static_cast<int>(DBG_LOG_DEBUG):
            DBG_LOGDEBUG(str);
            break;
        case static_cast<int>(DBG_LOG_INFO):
            DBG_LOGINFO(str);
            break;
        case static_cast<int>(DBG_LOG_WARN):
            DBG_LOGWARN(str);
            break;
        case static_cast<int>(DBG_LOG_ERROR):
            DBG_LOGERROR(str);
            break;
        default:
            DBG_LOGERROR(str);
    }
}

void MxmComLinkManager::LogChannelInfo()
{
    DBG_LOGDEBUG("---------Log All Channel Info-----------");
    for (auto &item : nodeChannelMap) {
        std::string debugInfo = "Node:" + item.first + ": ";
        for (auto &channel : item.second) {
            debugInfo += channel.second.ConvertMxmComChannelInfoToString();
        }
        DBG_LOGDEBUG(debugInfo);
    }
    for (auto &item : channelIdMap) {
        std::string debugInfo;
        debugInfo += item.second.ConvertMxmComChannelInfoToString();
        DBG_LOGDEBUG(debugInfo);
    }
}

HRESULT MxmComLinkManager::GetChannelByChannelId(uint64_t id, MxmComChannelInfo &channelInfo)
{
    ReadLocker<ReadWriteLock> channelLockGuard(&channelLock_);
    auto iter = channelIdMap.find(id);
    if (iter == channelIdMap.end()) {
        DBG_LOGWARN("No channel " << id);
        LogChannelInfo();
        return MXM_COM_ERROR_CHANNEL_NOT_FOUND;
    }
    channelInfo = iter->second;
    return HOK;
}

HRESULT MxmComLinkManager::GetChannelByRemoteNodeId(const std::string &nodeId, MxmChannelType chType,
                                                    MxmComChannelInfo &channelInfo)
{
    ReadLocker<ReadWriteLock> channelLockGuard(&channelLock_);
    auto iter = nodeChannelMap.find(nodeId);
    if (iter == nodeChannelMap.end()) {
        DBG_LOGERROR("No channel for node " << nodeId);
        LogChannelInfo();
        return MXM_COM_ERROR_CHANNEL_NOT_FOUND;
    }

    auto chTypeIter = iter->second.find(chType);
    if (chTypeIter == iter->second.end()) {
        DBG_LOGERROR("No channel for node " << nodeId << " type is " << static_cast<int>(chType));
        LogChannelInfo();
        return MXM_COM_ERROR_CHANNEL_NOT_FOUND;
    }
    channelInfo = chTypeIter->second;
    return HOK;
}

bool MxmComLinkManager::IsChannelExists(const std::string &nodeId, MxmChannelType chType)
{
    ReadLocker<ReadWriteLock> channelLockGuard(&channelLock_);
    auto iter = nodeChannelMap.find(nodeId);
    if (iter == nodeChannelMap.end()) {
        return false;
    }
    auto chTypeIter = iter->second.find(chType);
    if (chTypeIter == iter->second.end()) {
        return false;
    }
    return true;
}
void MxmComLinkManager::SetStop()
{
    WriteLocker<ReadWriteLock> channelLockGuard(&channelLock_);
    isStopped = true;
    DBG_LOGDEBUG("Set varible isStop, isStop=true");
}

bool MxmComLinkManager::IsStopped()
{
    ReadLocker<ReadWriteLock> channelLockGuard(&channelLock_);
    return isStopped;
}

void MxmComLinkManager::InsertChannel(MxmComChannelInfo &channelInfo)
{
    MxmComChannelStatePtr oldState;
    {
        WriteLocker<ReadWriteLock> channelLockGuard(&channelLock_);
        auto chType = channelInfo.GetChannelType();
        if (chType == MxmChannelType::SINGLE_SIDE && channelInfo.IsServerSide()) {
            const auto &channel = channelInfo.GetChannel();
            if (channel == nullptr) {
                DBG_LOGERROR("Cannot insert a null channel.");
                return;
            }
            auto channelId = channelInfo.GetChannelId();
            channelIdMap.emplace(channelId, channelInfo);
            DBG_LOGINFO("Insert channel id: " << channelId << ", cur node id"
                                              << channelInfo.GetConnectInfo().GetCurNodeId() << ", remote node id"
                                              << channelInfo.GetConnectInfo().GetRemoteNodeId());
            return;
        }
        const auto &nodeId = channelInfo.GetConnectInfo().GetRemoteNodeId();
        auto engineName = channelInfo.GetEngineName();
        auto nodeIter = nodeChannelMap.find(nodeId);
        auto oldChannelIter = nodeIter == nodeChannelMap.end() ?
                                  std::map<MxmChannelType, MxmComChannelInfo>::iterator{} :
                                  nodeIter->second.find(chType);
        if (nodeIter != nodeChannelMap.end() && oldChannelIter != nodeIter->second.end()) {
            DBG_LOGINFO("Engine " << engineName << " channel already exists, type is " << static_cast<uint16_t>(chType)
                                  << ", remote node is " << nodeId);
            const auto &oldChannel = oldChannelIter->second.GetChannel();
            if (oldChannel != nullptr) {
                oldState = oldChannel;
                channelIdMap.erase(oldChannelIter->second.GetChannelId());
            }
            nodeIter->second.erase(oldChannelIter);
        }
        auto iter = nodeChannelMap.find(nodeId);
        if (iter == nodeChannelMap.end()) {
            std::map<MxmChannelType, MxmComChannelInfo> map;
            map.emplace(chType, channelInfo);
            nodeChannelMap.emplace(nodeId, map);
        } else {
            iter->second.emplace(chType, channelInfo);
        }
        auto channelId = channelInfo.GetChannelId();
        channelIdMap.emplace(channelId, channelInfo);
        DBG_LOGINFO("Insert channel id: " << channelId << ", cur node id" << channelInfo.GetConnectInfo().GetCurNodeId()
                                          << ", remote node id" << channelInfo.GetConnectInfo().GetRemoteNodeId());
    }
    if (oldState != nullptr) {
        oldState->Close();
    }
}

void MxmComLinkManager::RemoveChannelByChannelId(uint64_t id)
{
    MxmComChannelInfo channelInfo;
    {
        WriteLocker<ReadWriteLock> channelLockGuard(&channelLock_);
        auto iter = channelIdMap.find(id);
        if (iter == channelIdMap.end()) {
            DBG_LOGERROR("No channel " << id << " ,can't remove");
            return;
        }
        channelInfo = iter->second;
        auto remoteId = iter->second.GetConnectInfo().GetRemoteNodeId();
        auto chType = iter->second.GetChannelType();
        auto nodeIter = nodeChannelMap.find(remoteId);
        if (nodeIter != nodeChannelMap.end()) {
            auto channelIter = nodeIter->second.find(chType);
            if (channelIter != nodeIter->second.end() &&
                (channelIter->second.GetChannel() == nullptr || channelIter->second.GetChannelId() == id)) {
                nodeIter->second.erase(channelIter);
            }
            if (nodeIter->second.empty()) {
                nodeChannelMap.erase(nodeIter);
            }
        }
        channelIdMap.erase(iter);
    }
    if (channelInfo.GetChannel() == nullptr) {
        DBG_LOGERROR("Channel " << id << " hcom NetChannelPtr is nullptr");
        return;
    }
    channelInfo.GetChannel()->Close();
}

void MxmComLinkManager::RemoveAllChannel()
{
    std::vector<MxmComChannelInfo> channels;
    {
        WriteLocker<ReadWriteLock> channelLockGuard(&channelLock_);
        for (const auto &entry : channelIdMap) {
            channels.push_back(entry.second);
        }
        nodeChannelMap.clear();
        channelIdMap.clear();
    }
    for (const auto &channel : channels) {
        if (channel.GetChannel() != nullptr) {
            channel.GetChannel()->Close();
        }
    }
}

std::map<std::string, MxmComEngine *> MxmComEngineManager::G_ENGINE_MAP;
std::mutex MxmComEngineManager::G_MUTEX;

static void HandlerWorkDefault(void (*handler)(MxmComMessageCtx &messageCtx), MxmComMessageCtx &messageCtx)
{
    if (handler != nullptr) {
        handler(messageCtx);
    }
}

MxmComEngine::MxmComEngine(MxmComEngineInfo engineInfo, ubs_hcom_service hcomNetService,
                           MxmComLinkStateNotify linkStateNotify)
    : engineInfo(std::move(engineInfo)),
      hcomNetService(hcomNetService),
      linkStateNotify(std::move(linkStateNotify))
{
}

MxmComEngine::~MxmComEngine()
{
    if (memPtr != 0) {
        ubs_hcom_mem_allocator_destroy(memPtr);
    }
}

const MxmComEngineInfo &MxmComEngine::GetEngineInfo() const
{
    return engineInfo;
}

HRESULT MxmComEngine::RegMxmComMsgHandler(const MxmComMsgHandler &handle)
{
    rwLock.LockWrite();
    if (handle.moduleCode >= MODULES_SIZE || handle.opCode >= OP_CODE_SIZE) {
        DBG_LOGERROR("Invalid module code or op code, module code is " << handle.moduleCode << ", op code is "
                                                                       << handle.opCode);
        rwLock.UnLock();
        return MXM_COM_ERROR_MESSAGE_INVALID_OP_CODE;
    }
    handlerMap[{handle.moduleCode, handle.opCode}] = handle;
    rwLock.UnLock();
    return HOK;
}

constexpr uint32_t SEND_RECEIVE_SIZE = 32 * 1024;
constexpr uint32_t SEND_RECEIVE_COUNT = 128;
constexpr uint32_t SEND_QUEUE_SIZE = 4096;
constexpr uint32_t CTX_STORE_CAP = 1024;
constexpr uint32_t MIN_BLOCK_SIZE = 4096;
constexpr uint32_t CACHE_TIER_COUNT = 10;
constexpr uint32_t CACHE_BLOCK_COUNT_PER_TIER = 8;
constexpr uint32_t BUCKET_COUNT = 8;
constexpr uint32_t QUEUE_SIZE = 2048;
constexpr uint32_t POST_RECEIVE_SIZE = 32;
constexpr uint32_t POLLING_SIZE = 1024;
constexpr uint32_t POST_SEND_COUNT = 1024;
constexpr uint32_t HB_IDLE_TIME = 2;
constexpr uint32_t MAX_SEND_RECV_DATA_COUNT = 256;
const std::string DEFAULT_DEVICE_IP_MASK = "127.0.0.1/24";
const std::string DEFAULT_WORKER_GROUP = "8";
constexpr int WORKER_THREAD_PRIORITY = -20;
constexpr uint16_t WORKER_THREAD_COUNT = 6;

static void AssignServiceOptions(const MxmComEngineInfo &engineInfo, ubs_hcom_service_options &options)
{
    if (engineInfo.IsUds() && engineInfo.GetEngineType() == MxmEngineType::CLIENT) {
        options.disableMemPoolThreadCache = 1;
    }
    options.workerGroupMode = static_cast<ubs_hcom_service_worker_mode>(engineInfo.GetWorkerMode());
    options.workerGroupThreadCount = WORKER_THREAD_COUNT;
    options.maxSendRecvDataSize = engineInfo.GetMaxSendReceiveSize() * SEND_RECEIVE_SIZE;
}

static void AssignHcomServiceOptions(const MxmComEngineInfo &engineInfo, ubs_hcom_service hcomNetService)
{
    ubs_hcom_service_set_lbpolicy(hcomNetService, SERVICE_ROUND_ROBIN);
    ubs_hcom_service_set_sq_size(hcomNetService, SEND_QUEUE_SIZE);
    ubs_hcom_service_set_rq_size(hcomNetService, QUEUE_SIZE);
    ubs_hcom_service_set_prepost_size(hcomNetService, POST_RECEIVE_SIZE);
    ubs_hcom_service_set_ctx_store_capacity(hcomNetService, CTX_STORE_CAP);
    ubs_hcom_service_set_max_connection_cnt(hcomNetService, engineInfo.GetMaxHcomConnectNum());
    ubs_hcom_service_set_max_send_recv_data_cnt(hcomNetService, MAX_SEND_RECV_DATA_COUNT);
    ubs_hcom_service_set_heartbeat_opt(hcomNetService, HB_IDLE_TIME, 7, 2);

    std::vector<std::string> ipMasks;
    if ((engineInfo.GetEngineType() == MxmEngineType::SERVER) &&
        (engineInfo.GetProtocol() == MxmProtocol::TCP || engineInfo.GetProtocol() == MxmProtocol::HCCS ||
         engineInfo.GetProtocol() == MxmProtocol::UB)) {
        ipMasks.push_back(engineInfo.GetIpInfo().ip + "/24");
    } else {
        ipMasks.push_back(DEFAULT_DEVICE_IP_MASK);
    }
    ubs_hcom_service_set_ipmask(hcomNetService, ipMasks.front().c_str());
}

const int16_t DEFAULT_REQUEST_TIMEOUT = 60;
const int16_t DEFAULT_HEARTBEAT_REQUEST_TIMEOUT = 0;
const auto ENV_COM_CHANNEL_TIMEOUT = "MXM_CHANNEL_TIMEOUT";

static int16_t GetIPCTimeOutFromEnv()
{
    auto envValue = std::getenv(ENV_COM_CHANNEL_TIMEOUT);
    uint64_t timeOut;
    if (envValue != nullptr && StrUtil::StrToULong(envValue, timeOut)) {
        DBG_LOGINFO("IPC timeout from varible, time=" << timeOut << "s");
        return static_cast<int16_t>(timeOut);
    }
    DBG_LOGINFO("IPC timeout from default, time=" << DEFAULT_REQUEST_TIMEOUT << "s");
    return DEFAULT_REQUEST_TIMEOUT;
}

HRESULT MxmComEngine::CreateChannel(MxmComChannelConnectInfo &info, MxmChannelType chType)
{
    DBG_LOGDEBUG("Start to create channel, remote node id=" << info.GetRemoteNodeId());
    ubs_hcom_service_connect_options options{};
    options.linkCount = info.GetLinkNum();
    auto engineName = engineInfo.GetName();
    if (hcomNetService == 0) {
        DBG_LOGERROR("Failed to create channel, engine " << engineName << "has not been initilized.");
        return MXM_COM_ERROR_ENGINE_NOT_INIT;
    }
    auto &remoteNodeId = info.GetRemoteNodeId();
    if (linkManager.IsChannelExists(remoteNodeId, chType)) {
        DBG_LOGDEBUG("Engine channel" << engineName
                                      << " has been already existed, type=" << static_cast<uint16_t>(chType));
        return HOK;
    }
    auto payload = ChannelTypeToPayload(info.GetCurNodeId(), chType);
    if (payload.size() >= sizeof(options.payLoad)) {
        return MXM_COM_ERROR_CONNECT_TO_PEER_FAIL;
    }
    if (memcpy_s(options.payLoad, sizeof(options.payLoad), payload.c_str(), payload.size() + 1) != 0) {
        DBG_LOGERROR("Failed to copy channel payload");
        return MXM_COM_ERROR_CONNECT_TO_PEER_FAIL;
    }
    ubs_hcom_channel channel = 0;
    auto url = engineInfo.GetProtocol() == MxmProtocol::UDS ? "uds://" + info.GetIp() :
                                                              "tcp://" + info.GetRemoteNodeId();
    auto ret = ubs_hcom_service_connect(hcomNetService, url.c_str(), &channel, options);
    if (ret != 0) {
        DBG_LOGERROR("Failed to connect remote, peer node=" << info.GetIp() << ":" << info.GetPort()
                                                            << ", ret=" << ret);
        return MXM_COM_ERROR_CONNECT_TO_PEER_FAIL;
    }
    if (channel == 0) {
        DBG_LOGERROR("Failed to create channel, pointer of channel is null");
        return HFAIL;
    }
    auto channelId = ubs_hcom_channel_get_id(channel);
    DBG_LOGINFO("Create channel successfully, engine=" << engineName << ", channel id=" << channelId);
    if (chType == MxmChannelType::HEARTBEAT) {
        ubs_hcom_channel_set_timeout(channel, DEFAULT_HEARTBEAT_REQUEST_TIMEOUT, DEFAULT_HEARTBEAT_REQUEST_TIMEOUT);
    } else {
        ubs_hcom_channel_set_timeout(channel, GetIPCTimeOutFromEnv(), GetIPCTimeOutFromEnv());
    }
    MxmComChannelStatePtr state;
    try {
        state = std::make_shared<MxmComChannelState>(hcomNetService, channel);
    } catch (...) {
        ubs_hcom_service_disconnect(hcomNetService, channel);
        return MXM_COM_ERROR_CONNECT_TO_PEER_FAIL;
    }
    MxmComChannelInfo chInfo(false, chType, engineName, state, info);

    if (engineInfo.GetProtocol() == MxmProtocol::UDS && engineInfo.IsServerSide()) {
        ubs_hcom_uds_id_info uds{};
        ubs_hcom_channel_get_remote_uds_id_info(channel, &uds);
        chInfo.SetUDSInfo({uds.pid, uds.uid, uds.gid});
    }
    linkManager.InsertChannel(chInfo);
    linkStateNotify(engineInfo, remoteNodeId, channelId, MxmLinkState::LINK_UP);
    return HOK;
}

HRESULT MxmComEngine::GetChannelByRemoteNodeId(const std::string &nodeId, MxmChannelType chType,
                                               MxmComChannelInfo &channelInfo)
{
    auto ret = linkManager.GetChannelByRemoteNodeId(nodeId, chType, channelInfo);
    return ret;
}

MxmComMsgHandler MxmComEngine::GetMessageHandler(uint16_t moduleCode, uint16_t opCode)
{
    rwLock.LockRead();
    if (moduleCode >= MODULES_SIZE || opCode >= OP_CODE_SIZE) {
        DBG_LOGERROR("Invalid module code or op code, module code is " << moduleCode << ", op code is " << opCode);
        rwLock.UnLock();
        return {};
    }
    auto iter = handlerMap.find({moduleCode, opCode});
    auto handler = iter == handlerMap.end() ? MxmComMsgHandler{} : iter->second;
    rwLock.UnLock();
    return handler;
}

void MxmComEngine::RemoveChannel(std::string remoteNodeId, MxmChannelType type)
{
    MxmComChannelInfo channelInfo{};
    HRESULT ret = linkManager.GetChannelByRemoteNodeId(remoteNodeId, type, channelInfo);
    if (ret != HOK) {
        DBG_LOGERROR("Failed to get channel info, error code is," << ret);
        return;
    }
    linkManager.RemoveChannelByChannelId(channelInfo.GetChannelId());
}

HRESULT MxmComEngine::PrepareMem()
{
    address = memalign(MIN_BLOCK_SIZE, engineInfo.GetMaxSendReceiveSize() * SEND_RECEIVE_SIZE);
    if (address == nullptr) {
        DBG_LOGERROR("Failed to alloc memory, maybe lack of spare memory in system.");
        return HFAIL;
    }

    ubs_hcom_memory_allocator_options options{};
    options.address = reinterpret_cast<uintptr_t>(address);
    options.size = engineInfo.GetMaxSendReceiveSize() * SEND_RECEIVE_SIZE;
    options.minBlockSize = MIN_BLOCK_SIZE;
    options.alignedAddress = true;
    options.cacheTierCount = CACHE_TIER_COUNT;
    options.cacheBlockCountPerTier = CACHE_BLOCK_COUNT_PER_TIER;
    options.bucketCount = BUCKET_COUNT;
    options.cacheTierPolicy = C_TIER_POWER;
    auto result = ubs_hcom_mem_allocator_create(C_DYNAMIC_SIZE_WITH_CACHE, &options, &memPtr);
    if (result != 0) {
        DBG_LOGERROR("Failed to create mem allocator, " << result);
    }
    return result;
}

static bool IsDirectory(const std::string &dir)
{
    struct stat info {};
    if (stat(dir.c_str(), &info) != 0) {
        return false; // 无法访问路径
    }
    return S_ISDIR(info.st_mode); // 检查是否为目录
}

static bool MkdirRecursive(const std::string &path, mode_t mode = 0755)
{
    size_t pos = 0;
    std::string dir;
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/') {
        dir = "/";
        pos = 1;
    }
    while ((pos = path.find('/', pos)) != std::string::npos) {
        dir = path.substr(0, pos);
        pos++;

        if (mkdir(dir.c_str(), mode) != 0) {
            if (errno != EEXIST) {
                return false;
            }
        }
    }
    if (mkdir(path.c_str(), mode) != 0) {
        return errno == EEXIST;
    }

    return true;
}

static HRESULT CheckAndCreateUdsPath(const MxmComEngineInfo &engineInfo)
{
    if (engineInfo.GetEngineType() != MxmEngineType::CLIENT && engineInfo.IsUds()) {
        if (!IsDirectory(MXM_IPC_UDS_PATH_PREFIX_DEFAULT)) { // 检查是否为目录
            if (MkdirRecursive(MXM_IPC_UDS_PATH_PREFIX_DEFAULT, UDS_PATH_PERM)) {
                DBG_LOGINFO("Success to create directory: " << MXM_IPC_UDS_PATH_PREFIX_DEFAULT);
            } else {
                DBG_LOGERROR("Failed to create directory: " << MXM_IPC_UDS_PATH_PREFIX_DEFAULT << " errno=" << errno);
                return HFAIL;
            }
        }
    }
    return HOK;
}

HRESULT MxmComEngine::Start()
{
    HRESULT ret = HOK;
    auto engineName = engineInfo.GetName();
    AssignHcomServiceOptions(engineInfo, hcomNetService);
    ret = RegisterEngineHandlers();
    if (ret != HOK) {
        return ret;
    }
    ret = RegisterTlsCallback();
    if (ret != HOK) {
        DBG_LOGERROR("Failed to register tls callback, " << ret);
        return ret;
    }
    ret = CheckAndCreateUdsPath(engineInfo);
    if (ret != HOK) {
        DBG_LOGERROR("Fail to create uds path. path: " << MXM_IPC_UDS_PATH_PREFIX_DEFAULT);
        return ret;
    }
    ret = ubs_hcom_service_start(hcomNetService);
    if (ret != 0) {
        DBG_LOGERROR("Create engine " << engineName << " failed, start service fail");
        return MXM_COM_ERROR_ENGINE_START_FAIL;
    }

    if (engineInfo.GetEngineType() != MxmEngineType::CLIENT && engineInfo.IsUds()) {
        // 设置uds文件权限
        const std::string udsPath = GetUdsPath(engineInfo.GetUdsInfo().first);
        const mode_t udsMode = engineInfo.GetUdsInfo().second;
        if ((udsMode & ~0777U) != 0 || (udsMode & 0002U) != 0) {
            DBG_LOGERROR("Refuse unsafe uds file permission: " << udsMode);
            return HFAIL;
        }
        if (chmod(udsPath.c_str(), udsMode) != 0) {
            DBG_LOGERROR("Failed to change uds file permission, " << strerror(errno));
            return HFAIL;
        }
    }

    if (engineInfo.IsUds()) {
        HRESULT retCode = PrepareMem();
        if (retCode != 0) {
            DBG_LOGERROR("Failed to prepare rndv mem, " << retCode);
            return retCode;
        }
        auto result =
            ubs_hcom_service_register_assign_memory_region(hcomNetService, reinterpret_cast<uintptr_t>(address),
                                                           engineInfo.GetMaxSendReceiveSize() * SEND_RECEIVE_SIZE, &mr);
        if (result != 0) {
            DBG_LOGERROR("Failed to register mem region, " << result);
            return result;
        }
        ubs_hcom_mr_info mrInfo{};
        result = ubs_hcom_service_get_memory_region_info(mr, &mrInfo);
        if (result != 0) {
            return result;
        }
        result = ubs_hcom_mem_allocator_set_mr_key(memPtr, mrInfo.lKey.keys[0]);
        if (result != 0) {
            DBG_LOGERROR("Failed to set memory allocator MR key, " << result);
            return result;
        }
    }
    return HOK;
}

int MxmComEngine::RegisterHandlerWork(const EngineHandlerWorker &handlerWorker)
{
    if (handlerWorker == nullptr) {
        DBG_LOGINFO("HandlerWorker is empty");
        return MXM_COM_ERROR_CHANNEL_NULL;
    }
    handlerWork = handlerWorker;
    return HOK;
}

HRESULT MxmComEngine::Stop()
{
    std::unique_lock<std::mutex> stopGuard(stopMutex);
    auto engineName = engineInfo.GetName();
    auto serviceToReap = hcomNetService;
    if (!stopPrepared) {
        deleted.store(true);
        linkManager.SetStop();
        if (callbackSlot != MxmComCallbackSlots::INVALID_SLOT) {
            MxmComCallbackSlots::Instance().BeginClose(callbackSlot);
            MxmComCallbackSlots::Instance().WaitCallbacks(callbackSlot);
        }
        linkManager.RemoveAllChannel();

        std::vector<std::thread> tasks;
        {
            std::lock_guard<std::mutex> lock(reconnectMutex);
            tasks.swap(reconnectTasks);
        }
        for (auto &task : tasks) {
            if (task.joinable()) {
                task.join();
            }
        }
        if (mr != 0) {
            ubs_hcom_service_destroy_memory_region(hcomNetService, mr);
            mr = 0;
        }
        if (memPtr != 0) {
            ubs_hcom_mem_allocator_destroy(memPtr);
            memPtr = 0;
        }
        stopPrepared = true;
    }

    if (hcomNetService != 0) {
        auto ret = ubs_hcom_service_destroy(hcomNetService, engineName.c_str());
        DBG_LOGINFO("Destroy hcom instance=" << engineName << " finish, ret=" << ret);
        if (ret != HOK) {
            return ret;
        }
        hcomNetService = 0;
    }
    if (callbackSlot != MxmComCallbackSlots::INVALID_SLOT) {
        MxmComCallbackSlots::Instance().ReleaseAfterServiceStopped(callbackSlot);
        callbackSlot = MxmComCallbackSlots::INVALID_SLOT;
    }
    if (address != nullptr) {
        free(address);
        address = nullptr;
    }
    stopGuard.unlock();
    if (serviceToReap != 0) {
        ReapReplies(serviceToReap);
    }
    return HOK;
}

int MxmComEngine::NewChannelThunk(void *engine, ubs_hcom_channel channel, const char *ipPort, const char *payload)
{
    return static_cast<MxmComEngine *>(engine)->NewChannel(ipPort == nullptr ? "" : ipPort, channel,
                                                           payload == nullptr ? "" : payload);
}

int MxmComEngine::BrokenThunk(ubs_hcom_channel channel, const char *, uint64_t usrCtx, const char *payload)
{
    try {
        auto *engine = reinterpret_cast<MxmComEngine *>(static_cast<uintptr_t>(usrCtx));
        engine->BrokenChannel(channel, payload);
        return HOK;
    } catch (...) {
        return HFAIL;
    }
}

int MxmComEngine::ReceivedThunk(ubs_hcom_service_context context, uint64_t usrCtx)
{
    try {
        return reinterpret_cast<MxmComEngine *>(static_cast<uintptr_t>(usrCtx))->ReceivedRequest(context);
    } catch (...) {
        return HFAIL;
    }
}

int MxmComEngine::SentThunk(ubs_hcom_service_context context, uint64_t usrCtx)
{
    try {
        return reinterpret_cast<MxmComEngine *>(static_cast<uintptr_t>(usrCtx))->SendRequest(context);
    } catch (...) {
        return HFAIL;
    }
}

int MxmComEngine::OneSideThunk(ubs_hcom_service_context context, uint64_t usrCtx)
{
    try {
        return reinterpret_cast<MxmComEngine *>(static_cast<uintptr_t>(usrCtx))->OneSideDoneRequest(context);
    } catch (...) {
        return HFAIL;
    }
}

HRESULT MxmComEngine::RegisterEngineHandlers()
{
    auto usrCtx = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
    bool isOobSvr = engineInfo.GetEngineType() != MxmEngineType::CLIENT;
    if (isOobSvr) {
        callbackSlot = MxmComCallbackSlots::Instance().Acquire(this, NewChannelThunk);
        if (callbackSlot == MxmComCallbackSlots::INVALID_SLOT ||
            !MxmComCallbackSlots::Instance().Activate(callbackSlot)) {
            DBG_LOGERROR("No callback slot available for engine " << engineInfo.GetName());
            return MXM_COM_ERROR_ENGINE_CREATE_FAIL;
        }
        std::string url;
        if (engineInfo.IsUds()) {
            url = "uds://" + GetUdsPath(engineInfo.GetUdsInfo().first) + ":" +
                  std::to_string(engineInfo.GetUdsInfo().second);
        } else {
            url = "tcp://" + engineInfo.GetIpInfo().ip + ":" + std::to_string(engineInfo.GetIpInfo().port);
        }
        auto ret = ubs_hcom_service_bind(hcomNetService, url.c_str(),
                                         MxmComCallbackSlots::Instance().GetCallback(callbackSlot));
        if (ret != HOK) {
            return ret;
        }
    }
    ubs_hcom_service_register_broken_handler(hcomNetService, BrokenThunk, C_CHANNEL_BROKEN_ALL, usrCtx);
    ubs_hcom_service_register_handler(hcomNetService, C_SERVICE_REQUEST_RECEIVED, ReceivedThunk, usrCtx);
    ubs_hcom_service_register_handler(hcomNetService, C_SERVICE_REQUEST_POSTED, SentThunk, usrCtx);
    ubs_hcom_service_register_handler(hcomNetService, C_SERVICE_READWRITE_DONE, OneSideThunk, usrCtx);
    return HOK;
}

static void EraseCapiKeypass(char *addr, int len)
{
    UbsCryptorHandler::GetInstance().EraseDecryptData(addr, len);
    DBG_LOGINFO("TLS erase key pass finished.");
}

static int GetCapiCa(const char *name, char **caPath, char **crlPath, ubs_hcom_peer_cert_verify_type *verifyType,
                     ubs_hcom_tls_cert_verify *verify)
{
    if (caPath == nullptr || crlPath == nullptr || verifyType == nullptr || verify == nullptr) {
        return -1;
    }
    try {
        auto &config = UbsCommonConfig::GetInstance();
        *caPath = const_cast<char *>(config.GetCaPath().c_str());
        *crlPath = config.GetCrlPath().empty() ? nullptr : const_cast<char *>(config.GetCrlPath().c_str());
        *verifyType = C_VERIFY_BY_DEFAULT;
        *verify = nullptr;
        DBG_LOGINFO("TLS get ca path finished, peer name: " << (name == nullptr ? "" : name));
        return 0;
    } catch (...) {
        return -1;
    }
}

static int GetCapiCert(const char *name, char **certPath)
{
    if (certPath == nullptr) {
        return -1;
    }
    try {
        *certPath = const_cast<char *>(UbsCommonConfig::GetInstance().GetCertPath().c_str());
        DBG_LOGINFO("TLS get cert path finished, peer name: " << (name == nullptr ? "" : name));
        return 0;
    } catch (...) {
        return -1;
    }
}

static int GetCapiPrivateKey(const char *name, char **path, char **pw, int *length, ubs_hcom_tls_keypass_erase *erase)
{
    if (path == nullptr || pw == nullptr || length == nullptr || erase == nullptr) {
        return -1;
    }
    *path = nullptr;
    *pw = nullptr;
    *length = 0;
    *erase = nullptr;
    try {
        auto &config = UbsCommonConfig::GetInstance();
        std::pair<char *, int> pwPair;
        auto ret = UbsCryptorHandler::GetInstance().Decrypt(0, config.GetKeypassPath(), pwPair);
        if (ret != 0) {
            DBG_LOGERROR("Decrypt failed, ret: " << ret << ", peer name: " << (name == nullptr ? "" : name));
            return ret;
        }
        *path = const_cast<char *>(config.GetKeyPath().c_str());
        *pw = pwPair.first;
        *length = pwPair.second;
        *erase = EraseCapiKeypass;
        DBG_LOGINFO("TLS get private key path finished, peer name: " << (name == nullptr ? "" : name));
        return 0;
    } catch (...) {
        return -1;
    }
}

HRESULT MxmComEngine::RegisterTlsCallback()
{
    if (!engineInfo.GetEnableTls()) {
        ubs_hcom_service_set_tls_opt(hcomNetService, false, C_SERVICE_TLS_1_3, engineInfo.GetCipherSuite(), nullptr,
                                     nullptr, nullptr, nullptr, nullptr);
        return 0;
    }

    auto ret = UbsCryptorHandler::SetCryptorLogger(Logger);
    if (ret != 0) {
        DBG_LOGERROR("Set cryptor logger failed, ret: " << ret);
        return ret;
    }

    ubs_hcom_service_set_tls_opt(hcomNetService, true, C_SERVICE_TLS_1_3, engineInfo.GetCipherSuite(), GetCapiCert,
                                 GetCapiPrivateKey, GetCapiCa, nullptr, nullptr);
    DBG_LOGINFO("Register tls callback finished.");
    return 0;
}

HRESULT MxmComEngine::NewChannel(const std::string &ipPort, ubs_hcom_channel ch, const std::string &payload)
{
    if (linkManager.IsStopped()) {
        DBG_LOGERROR("linkManager is stopping, cannot new channel.");
        return MXM_COM_ERROR_CHANNEL_NULL;
    }
    const auto &engineName = engineInfo.GetName();
    bool isUds = engineInfo.IsUds();
    DBG_LOGINFO("Engine " << engineName << " get new channel");
    if (ch == 0) {
        DBG_LOGERROR("NetChannelPtr of NewChannel is nullptr ipPort is: " << ipPort << " payload is: " << payload);
        return MXM_COM_ERROR_CHANNEL_NULL;
    }
    std::pair<std::string, MxmChannelType> payLoadPair = SplitPayload(payload);
    if (payLoadPair.first.empty()) {
        DBG_LOGERROR("Empty payLoad pair name is: " << payload);
        return MXM_COM_ERROR_INVALID_CHANNEL_TYPE;
    }
    auto channelId = ubs_hcom_channel_get_id(ch);
    DBG_LOGINFO("New channel " << channelId << " from: " << ipPort << ", payload is: " << payload);
    MxmComChannelConnectInfo connectInfo;
    connectInfo.SetCurNodeId(engineInfo.GetNodeId());
    connectInfo.SetRemoteNodeId(payLoadPair.first);
    connectInfo.SetIsUds(isUds);

    if (payLoadPair.second == MxmChannelType::HEARTBEAT) {
        ubs_hcom_channel_set_timeout(ch, DEFAULT_HEARTBEAT_REQUEST_TIMEOUT, DEFAULT_HEARTBEAT_REQUEST_TIMEOUT);
    } else {
        ubs_hcom_channel_set_timeout(ch, GetIPCTimeOutFromEnv(), GetIPCTimeOutFromEnv());
    }
    // Keep a separate reference for the published state until the callback's reference is returned.
    ubs_hcom_channel_refer(ch);
    MxmComChannelStatePtr state;
    try {
        state = std::make_shared<MxmComChannelState>(hcomNetService, ch);
        MxmComChannelInfo chInfo(true, payLoadPair.second, engineName, state, connectInfo);
        if (engineInfo.GetProtocol() == MxmProtocol::UDS && engineInfo.IsServerSide()) {
            ubs_hcom_uds_id_info uds{};
            ubs_hcom_channel_get_remote_uds_id_info(ch, &uds);
            chInfo.SetUDSInfo({uds.pid, uds.uid, uds.gid});
        }
        linkManager.InsertChannel(chInfo);
        DBG_LOGINFO("Insert channel " << channelId << " from [" << ipPort << "," << payload << "] success");
        if (payLoadPair.second != MxmChannelType::SINGLE_SIDE) {
            linkStateNotify(engineInfo, payLoadPair.first, channelId, MxmLinkState::LINK_UP);
        }
    } catch (...) {
        if (state != nullptr) {
            linkManager.RemoveChannelByChannelId(channelId);
            state->Close();
        } else {
            ubs_hcom_channel_derefer(ch);
        }
        throw;
    }
    ubs_hcom_channel_derefer(ch);
    return HOK;
}

void MxmComEngine::BrokenChannel(ubs_hcom_channel ch, const char *payload)
{
    const auto &engineName = engineInfo.GetName();
    DBG_LOGINFO("Engine " << engineName << " channel broken");
    if (ch == 0) {
        DBG_LOGERROR("Engine " << engineName << " channel broken, and channel is nullptr");
        return;
    }
    auto channelId = ubs_hcom_channel_get_id(ch);
    DBG_LOGINFO("Engine " << engineName << " channel broken, id: " << channelId);
    std::pair<std::string, MxmChannelType> payLoadPair = SplitPayload(payload == nullptr ? "" : payload);
    MxmComChannelInfo channelInfo;

    auto ret = linkManager.GetChannelByChannelId(channelId, channelInfo);
    if (ret != 0) {
        DBG_LOGWARN("Channel : " << channelId << " not found in MxmTransLinkManager.");
        return;
    }
    MxmUdsIdInfo uds = channelInfo.GetUDSInfo();
    linkManager.RemoveChannelByChannelId(channelId);
    linkStateNotify(engineInfo, channelInfo.GetConnectInfo().GetRemoteNodeId(), uds.pid, MxmLinkState::LINK_DOWN);
    auto isReconnectHook = engineInfo.GetReConnectHook();
    // 配置是否重连回调且回调返回false，则不进行重连；未配置重连回调，或者回调返回true则默认重连
    if (isReconnectHook != nullptr &&
        !isReconnectHook(channelInfo.GetConnectInfo().GetRemoteNodeId(), payLoadPair.second)) {
        return;
    }
    std::lock_guard<std::mutex> lock(reconnectMutex);
    if (!deleted.load()) {
        reconnectTasks.emplace_back([channelInfo, this]() { DoReconnect(channelInfo); });
    }
}

int MxmComEngine::ConvertContextToMessageCtx(ubs_hcom_service_context context, MxmComMessageCtx &msgCtx)
{
    ubs_hcom_channel_reply_context replyCtx{};
    if (ubs_hcom_context_get_rspctx(context, &replyCtx) != HOK) {
        return -1;
    }
    MxmUdsIdInfo udsIdInfo{0, 0, 0};
    if (engineInfo.IsUds()) {
        GetUdsInfoFromNetServiceContext(context, udsIdInfo);
    }
    msgCtx.SetUdsInfo(udsIdInfo);
    auto msg = GetMessageFromNetServiceContext(context);

    auto msgPtr = static_cast<MxmComMessagePtr>(static_cast<void *>(msg));
    msgCtx.SetEngineName(engineInfo.GetName());
    msgCtx.SetChannelId(GetChannelIdFromNetServiceContext(context));
    auto ret = msgCtx.SetMessage(msgPtr, ubs_hcom_context_get_datalen(context));
    if (ret != 0) {
        DBG_LOGERROR("SetMessage failed. ret=" << ret);
        return -1;
    }
    msgCtx.SetRspCtx(reinterpret_cast<uintptr_t>(replyCtx.rspCtx));
    return 0;
}

HRESULT MxmComEngine::ReceivedRequest(ubs_hcom_service_context context)
{
    TP_TRACE_BEGIN(TP_UBSM_GET_HANDLER);
    auto engineName = engineInfo.GetName();
    DBG_LOGDEBUG("Engine " << engineName << " get new request");
    auto msg = GetMessageFromNetServiceContext(context);
    if (UNLIKELY(msg == nullptr)) {
        DBG_LOGERROR("Engine " << engineName << " get message is nullptr");
        return MXM_COM_ERROR_MESSAGE_INVALID;
    }
    if (UNLIKELY(!CheckMessageBodyLen(context, *msg))) {
        DBG_LOGERROR("Engine " << engineName << " check message size failed.");
        return MXM_COM_ERROR_MESSAGE_CHECK_SIZE_FAIL;
    }
    auto crc = msg->GetMessageHead().GetCrc();
    auto moduleCode = msg->GetMessageHead().GetModuleCode();
    auto opCode = msg->GetMessageHead().GetOpCode();
    auto crcNew = CrcUtil::SoftCrc32(msg->GetMessageBody(), msg->GetMessageBodyLen(), SHIFT_1);
    DBG_LOGDEBUG("Engine " << engineName << " get new request, op: " << opCode << ", module: " << moduleCode
                           << ", msg body len is :" << msg->GetMessageBodyLen() << ", crc is :" << crc
                           << ", crc new is:" << crcNew);

    if (crc != crcNew) {
        DBG_LOGERROR("Engine " << engineName << " check crc failed, op: " << opCode << ", module: " << moduleCode
                               << ", msg body len is :" << msg->GetMessageBodyLen() << ", crc is :" << crc
                               << ", crc new is:" << crcNew);
        return MXM_COM_ERROR_MESSAGE_CHECK_SIZE_FAIL;
    }
    MxmComMessageCtx msgCtx;
    auto ret = ConvertContextToMessageCtx(context, msgCtx);
    if (ret != 0) {
        DBG_LOGERROR("ConvertContextToMessageCtx failed. ret=" << ret);
        return MXM_COM_ERROR_MESSAGE_INVALID;
    }
    auto handler = GetMessageHandler(moduleCode, opCode);
    if (handler.handler == nullptr) {
        DBG_LOGERROR("Engine " << engineName << ", no handler for module " << moduleCode << ", op code " << opCode
                               << ", crc " << crc);
        msgCtx.FreeMessage();
        return MXM_COM_ERROR_MESSAGE_INVALID_OP_CODE;
    }
    TP_TRACE_END(TP_UBSM_GET_HANDLER, 0);
    if (handlerWork != nullptr) {
        handlerWork(handler.handler, msgCtx);
    } else {
        msgCtx.FreeMessage();
        DBG_LOGWARN("Handler is empty, can not response, module code=" << moduleCode << "opcode=" << opCode);
    }
    return HOK;
}

HRESULT MxmComEngine::GetChannelById(uint64_t channelId, MxmComChannelInfo &channelInfo)
{
    auto ret = linkManager.GetChannelByChannelId(channelId, channelInfo);
    return ret;
}

HRESULT MxmComEngine::SendRequest(ubs_hcom_service_context context)
{
    int ret = 0;
    ubs_hcom_context_get_result(context, &ret);
    DBG_LOGINFO("Send request finish, result is " << ret);
    return HOK;
}

HRESULT MxmComEngine::OneSideDoneRequest(ubs_hcom_service_context context)
{
    int ret = 0;
    ubs_hcom_context_get_result(context, &ret);
    DBG_LOGINFO("One side done finish, result is " << ret);
    return HOK;
}

void MxmComEngine::DoReconnect(const MxmComChannelInfo &channelInfo)
{
    DBG_LOGINFO("Start reconnect task, engine " << channelInfo.GetEngineName() << ", remote is "
                                                << channelInfo.GetConnectInfo().GetRemoteNodeId());
    auto ret = pthread_setname_np(pthread_self(), "Reconnect");
    if (ret != 0) {
        DBG_LOGERROR("Set thread name fail," << ret);
    }
    int waitSeconds = 1;
    while (!deleted.load()) {
        auto result = CreateChannel(const_cast<MxmComChannelConnectInfo &>(channelInfo.GetConnectInfo()),
                                    channelInfo.GetChannelType());
        if (result == 0) {
            break;
        }
        DBG_LOGERROR("Reconnect fail, engine " << channelInfo.GetEngineName() << ", remote is "
                                               << channelInfo.GetConnectInfo().GetRemoteNodeId());
        std::this_thread::sleep_for(std::chrono::seconds(waitSeconds));
    }
    if (!deleted.load() && this->postReconnectHandler) {
        DBG_LOGINFO("Reconnect successfully. Start to check invoke postReconnectHandler.");
        ret = postReconnectHandler();
        if (ret != 0) {
            DBG_LOGERROR("Failed to invoke postReconnectHandler, ret=" << ret);
            return;
        }
        DBG_LOGINFO("Success to invoke postReconnectHandler.");
    }
    DBG_LOGINFO("Reconnect task finish, engine " << channelInfo.GetEngineName() << ", remote is "
                                                 << channelInfo.GetConnectInfo().GetRemoteNodeId());
}

HRESULT MxmComEngineManager::CreateEngine(const MxmComEngineInfo &engineInfo, const MxmComLinkStateNotify &notify,
                                          const EngineHandlerWorker &handlerWorker)
{
    std::lock_guard<std::mutex> locker(G_MUTEX);
    const auto &engineName = engineInfo.GetName();
    auto iter = G_ENGINE_MAP.find(engineName);
    if (iter != G_ENGINE_MAP.end()) {
        DBG_LOGINFO("Engine " << engineName << " has already been created");
        return HOK;
    }
    ubs_hcom_set_log_handler(engineInfo.GetLogFunc());
    MxmProtocol hcomProtocol = engineInfo.GetProtocol();
    ubs_hcom_service_options options{};
    AssignServiceOptions(engineInfo, options);
    ubs_hcom_service_type serviceType = C_SERVICE_TCP;
    if (hcomProtocol == MxmProtocol::UDS) {
        serviceType = C_SERVICE_UDS;
    } else if (hcomProtocol == MxmProtocol::HCCS) {
        serviceType = C_SERVICE_SHM;
    } else if (hcomProtocol == MxmProtocol::UB) {
        serviceType = C_SERVICE_UBC;
    }
    ubs_hcom_service service = 0;
    auto createRet = ubs_hcom_service_create(serviceType, engineName.c_str(), options, &service);
    if (UNLIKELY(createRet != HOK || service == 0)) {
        DBG_LOGERROR("Failed to create engine, name=" << engineName << "error=no memory");
        return MXM_COM_ERROR_ENGINE_CREATE_FAIL;
    }
    auto engine = new (std::nothrow) MxmComEngine(engineInfo, service, notify);
    if (engine == nullptr) {
        DBG_LOGERROR("Failed to create engine, error=no memory");
        ubs_hcom_service_destroy(service, engineName.c_str());
        return MXM_COM_ERROR_ENGINE_CREATE_FAIL;
    }

    int hr = engine->RegisterHandlerWork(handlerWorker);
    if (hr != HOK) {
        DBG_LOGWARN("Failed to rgister worker, ret=" << hr);
    }

    auto ret = engine->Start();
    if (ret != 0) {
        DBG_LOGERROR("Failed to start engine, ret=" << ret);
        auto stopRet = engine->Stop();
        if (stopRet != HOK) {
            G_ENGINE_MAP.emplace(engineName, engine);
            return ret;
        }
        delete engine;
        return ret;
    }
    G_ENGINE_MAP.emplace(engineName, engine);
    DBG_LOGINFO("Create engine=" << engineName << " successfully");
    return HOK;
}

HRESULT MxmComEngineManager::DeleteEngine(const std::string &name)
{
    MxmComEngine *enginePtr;
    {
        std::lock_guard<std::mutex> locker(G_MUTEX);
        auto iter = G_ENGINE_MAP.find(name);
        if (iter == G_ENGINE_MAP.end()) {
            DBG_LOGWARN("Engine " << name << "has not been created or already been destroyed");
            return HOK;
        }
        enginePtr = iter->second;
        G_ENGINE_MAP.erase(name);
    }
    if (enginePtr != nullptr) {
        auto ret = enginePtr->Stop();
        if (ret != HOK) {
            std::lock_guard<std::mutex> locker(G_MUTEX);
            G_ENGINE_MAP.emplace(name, enginePtr);
            return ret;
        }
        delete enginePtr;
        enginePtr = nullptr;
        return HOK;
    }
    return HOK;
}

void MxmComEngineManager::DeleteAllEngine()
{
    std::map<std::string, MxmComEngine *> engines;
    {
        std::lock_guard<std::mutex> locker(G_MUTEX);
        engines.swap(G_ENGINE_MAP);
    }
    for (const auto &entry : engines) {
        if (entry.second->Stop() == HOK) {
            delete entry.second;
        } else {
            std::lock_guard<std::mutex> locker(G_MUTEX);
            G_ENGINE_MAP.emplace(entry.first, entry.second);
        }
    }
}

MxmComEngine *MxmComEngineManager::GetEngine(const std::string &name)
{
    std::lock_guard<std::mutex> locker(G_MUTEX);
    auto iter = G_ENGINE_MAP.find(name);
    if (iter == G_ENGINE_MAP.end()) {
        DBG_LOGERROR("Engine " << name << "has not been created");
        return nullptr;
    }
    return iter->second;
}

HRESULT CreateChannel(bool isUds, const std::string &engineName, const std::pair<std::string, uint16_t> &ipAndPort,
                      const std::pair<std::string, std::string> &nodeIds, MxmChannelType chType)
{
    if (ipAndPort.first.empty()) {
        DBG_LOGERROR("connect ip or udsPath is empty");
        return HFAIL;
    }
    if (nodeIds.first.empty() || nodeIds.second.empty()) {
        DBG_LOGERROR("Node id is empty, current node id=" << nodeIds.first << ", remote node id=" << nodeIds.second);
        return HFAIL;
    }
    MxmComEngine *engine = MxmComEngineManager::GetEngine(engineName);
    if (engine == nullptr) {
        DBG_LOGERROR("Failed to get engine, engine name=" << engineName);
        return MXM_COM_ERROR_GET_ENGINE_FAIL;
    }
    DBG_LOGDEBUG("Creating Channel, current node id=" << nodeIds.first << ", remote node id=" << nodeIds.second);
    MxmComChannelConnectInfo connectInfo(isUds, ipAndPort.first, ipAndPort.second, nodeIds.second, nodeIds.first);
    connectInfo.SetLinkNum(SHIFT_1);
    return engine->CreateChannel(connectInfo, chType);
}

struct PendingReply {
    PendingReply(ubs_hcom_service owner, const MxmComCallback &cb) : service(owner), callback(cb) {}
    ubs_hcom_service service;
    MxmComCallback callback;
    std::atomic<bool> notified{false};
};

static std::mutex G_REPLY_MUTEX;
static std::atomic<uintptr_t> G_REPLY_TOKEN{1};
static std::map<uintptr_t, std::shared_ptr<PendingReply>> G_PENDING_REPLIES;

static void NotifyReply(const std::shared_ptr<PendingReply> &state, void *data, uint32_t size, int result)
{
    if (!state->notified.exchange(true)) {
        try {
            state->callback.cb(state->callback.cbCtx, data, size, result);
        } catch (...) {
            DBG_LOGERROR("Reply completion callback failed");
        }
    }
}

static void ReplyDone(void *arg, ubs_hcom_service_context context)
{
    std::shared_ptr<PendingReply> state;
    {
        std::lock_guard<std::mutex> guard(G_REPLY_MUTEX);
        auto iter = G_PENDING_REPLIES.find(reinterpret_cast<uintptr_t>(arg));
        if (iter == G_PENDING_REPLIES.end()) {
            return;
        }
        state = iter->second;
        G_PENDING_REPLIES.erase(iter);
    }
    int result = HFAIL;
    ubs_hcom_context_get_result(context, &result);
    NotifyReply(state, ubs_hcom_context_get_data(context), ubs_hcom_context_get_datalen(context), result);
}

static void ReapReplies(ubs_hcom_service service)
{
    std::vector<std::shared_ptr<PendingReply>> abandoned;
    {
        std::lock_guard<std::mutex> guard(G_REPLY_MUTEX);
        for (auto iter = G_PENDING_REPLIES.begin(); iter != G_PENDING_REPLIES.end();) {
            if (iter->second->service == service) {
                abandoned.push_back(iter->second);
                iter = G_PENDING_REPLIES.erase(iter);
            } else {
                ++iter;
            }
        }
    }
    for (const auto &state : abandoned) {
        NotifyReply(state, nullptr, 0, MXM_COM_ERROR_REPLY_FAIL);
    }
}

HRESULT GetChannel(const std::string &engineName, MxmComMessageCtx &message, MxmComChannelState::Reference &channel,
                   uint32_t &sendLen)
{
    auto *transMsg = static_cast<MxmComMessage *>(static_cast<void *>(message.GetMessage()));
    if (transMsg == nullptr) {
        DBG_LOGERROR("The trans msg is nullptr.");
        return MXM_COM_ERROR_MESSAGE_INVALID;
    }
    const std::string &nodeId = message.GetDstId();
    MxmComChannelInfo channelInfo;
    auto engine = MxmComEngineManager::GetEngine(engineName);
    if (engine == nullptr) {
        DBG_LOGERROR("get engine failed, engineName: " << engineName);
        return MXM_COM_ERROR_GET_ENGINE_FAIL;
    }
    HRESULT res = engine->GetChannelByRemoteNodeId(nodeId, message.GetChannelType(), channelInfo);
    if (res != 0) {
        DBG_LOGERROR("Channel info is abnormal, nodeId: " << nodeId);
        return MXM_COM_ERROR_CHANNEL_NOT_FOUND;
    }
    channel = channelInfo.GetChannel() == nullptr ? MxmComChannelState::Reference{} :
                                                    channelInfo.GetChannel()->Acquire();
    if (!channel) {
        DBG_LOGERROR("Channel is nullptr, nodeId: " << nodeId);
        return MXM_COM_ERROR_CHANNEL_NULL;
    }
    sendLen = static_cast<uint32_t>(sizeof(MxmComMessage)) + transMsg->GetMessageBodyLen();
    return HOK;
}

HRESULT MxmCommunication::CreateMxmComEngine(const MxmComEngineInfo &engine, const MxmComLinkStateNotify &notify,
                                             const EngineHandlerWorker &handlerWorker)
{
    return MxmComEngineManager::CreateEngine(engine, notify, handlerWorker);
}

HRESULT MxmCommunication::DeleteMxmComEngine(const std::string &name)
{
    return MxmComEngineManager::DeleteEngine(name);
}

HRESULT MxmCommunication::MxmComRpcConnect(const std::string &engineName, const RpcNode &remoteNodeId,
                                           const std::string &nodeId, MxmChannelType chType)
{
    DBG_LOGDEBUG("rpc connect start, curNodeId is " << nodeId << " remoteNodeId is " << remoteNodeId.ip);
    std::pair<std::string, uint16_t> ipAndPort = std::make_pair(remoteNodeId.ip, remoteNodeId.port);
    std::pair<std::string, std::string> nodeIds = std::make_pair(nodeId, remoteNodeId.name);
    HRESULT res = CreateChannel(false, engineName, ipAndPort, nodeIds, chType);
    if (res != 0) {
        DBG_LOGERROR("create rpc_connect channel failed," << res);
        return HFAIL;
    }
    DBG_LOGDEBUG("rpc_connect channel success, curNodeId: " << nodeId << " remoteNodeId:" << remoteNodeId.ip);
    return HOK;
}

HRESULT MxmCommunication::MxmComIpcConnect(const std::string &engineName, const std::string &udsPath,
                                           const std::string &nodeId, MxmChannelType chType)
{
    DBG_LOGINFO("ipc connect start, nodeId=" << nodeId << ", udsName=" << udsPath);
    std::string udsAbsolutePath = GetUdsPath(udsPath);
    const std::pair<std::string, uint16_t> &ipAndPort = std::make_pair(udsAbsolutePath, 0);
    const std::pair<std::string, std::string> &nodeIds = std::make_pair(nodeId, nodeId);
    HRESULT res = CreateChannel(true, engineName, ipAndPort, nodeIds, chType);
    if (res != 0) {
        DBG_LOGERROR("Failed to create channel, ret=" << res << ", engineName=" << engineName << ", nodeId=" << nodeId);
        return HFAIL;
    }
    DBG_LOGINFO("create ipc_connect channel successfully, engineName=" << engineName << ", nodeId=" << nodeId);
    return HOK;
}

HRESULT MxmCommunication::RegMxmComMsgHandler(const std::string &engineName, const MxmComMsgHandler &handle)
{
    MxmComEngine *engine = MxmComEngineManager::GetEngine(engineName);
    if (engine == nullptr) {
        DBG_LOGERROR("get engine failed, engineName: " << engineName);
        return MXM_COM_ERROR_GET_ENGINE_FAIL;
    }
    return engine->RegMxmComMsgHandler(handle);
}

HRESULT MxmCommunication::MxmComMsgSend(const std::string &engineName, MxmComMessageCtx &message,
                                        MxmComDataDesc &retData)
{
    MxmComChannelState::Reference channel;
    uint32_t sendLen;
    HRESULT channelRes = GetChannel(engineName, message, channel, sendLen);
    if (channelRes != HOK) {
        return channelRes;
    }
    ubs_hcom_channel_request reqMsg{};
    reqMsg.address = message.GetMessage();
    reqMsg.size = sendLen;
    ubs_hcom_channel_response rspMsg{};

    TP_TRACE_BEGIN(TP_UBSM_HCOM_CALL);
    auto ret = ubs_hcom_channel_call(channel.Get(), reqMsg, &rspMsg, nullptr);
    TP_TRACE_END(TP_UBSM_HCOM_CALL, ret);
    if (ret != 0) {
        DBG_LOGERROR("Channel syncCallRaw failed, ret," << ret);
        return MXM_COM_ERROR_SYNC_CALL_FAIL;
    }
    retData.data = static_cast<uint8_t *>(rspMsg.address);
    retData.len = rspMsg.size;
    return HOK;
}

void MxmCommunication::MxmComMsgReply(MxmComMessageCtx &message, const MxmComDataDesc &data,
                                      const MxmComCallback &usrCb)
{
    uintptr_t rspCtx = message.GetRspCtx();
    const std::string &nodeId = message.GetDstId();
    const auto &engineName = message.GetEngineName();
    auto engine = MxmComEngineManager::GetEngine(engineName);
    if (engine == nullptr) {
        DBG_LOGERROR("Reply fail, get engine failed, engineName: " << engineName << ", channel id "
                                                                   << message.GetChannelId());
        return;
    }
    MxmComChannelInfo channelInfo;
    HRESULT res = engine->GetChannelById(message.GetChannelId(), channelInfo);
    if (res != 0) {
        DBG_LOGERROR("Channel info is abnormal, channel id: " << message.GetChannelId());
        return;
    }
    auto channel = channelInfo.GetChannel() == nullptr ? MxmComChannelState::Reference{} :
                                                         channelInfo.GetChannel()->Acquire();
    if (!channel) {
        DBG_LOGERROR("Channel is nullptr, nodeId: " << nodeId;
                     usrCb.cb(usrCb.cbCtx, nullptr, 0, MXM_COM_ERROR_CHANNEL_NULL));
        return;
    }
    auto replyHook = message.GetReplyFuncHook();
    if (replyHook != nullptr) {
        res = replyHook(data, usrCb);
    } else {
        if (usrCb.cb == nullptr) {
            return;
        }
        std::shared_ptr<PendingReply> pending;
        uintptr_t token = 0;
        bool atCapacity = false;
        try {
            pending = std::make_shared<PendingReply>(engine->GetService(), usrCb);
            {
                std::lock_guard<std::mutex> guard(G_REPLY_MUTEX);
                atCapacity = G_PENDING_REPLIES.size() >= 4 * CTX_STORE_CAP;
                if (!atCapacity) {
                    token = G_REPLY_TOKEN.fetch_add(1);
                    G_PENDING_REPLIES.emplace(token, pending);
                }
            }
        } catch (...) {
            usrCb.cb(usrCb.cbCtx, nullptr, 0, MXM_COM_ERROR_NEW_NET_CALLBACK_FAIL);
            return;
        }
        if (atCapacity) {
            usrCb.cb(usrCb.cbCtx, nullptr, 0, MXM_COM_ERROR_REPLY_FAIL);
            return;
        }
        ubs_hcom_channel_request reqMsg{};
        reqMsg.address = data.data;
        reqMsg.size = data.len;
        ubs_hcom_channel_reply_context replyCtx{};
        replyCtx.rspCtx = reinterpret_cast<void *>(rspCtx);
        ubs_hcom_channel_callback callback{ReplyDone, reinterpret_cast<void *>(token)};
        res = ubs_hcom_channel_reply(channel.Get(), reqMsg, replyCtx, &callback);
        if (res != HOK) {
            {
                std::lock_guard<std::mutex> guard(G_REPLY_MUTEX);
                G_PENDING_REPLIES.erase(token);
            }
            NotifyReply(pending, nullptr, 0, MXM_COM_ERROR_REPLY_FAIL);
        }
        return;
    }
    if (res != 0) {
        DBG_LOGERROR("Channel sendRaw failed, res: " << res << " nodeId: " << nodeId);
        usrCb.cb(usrCb.cbCtx, nullptr, 0, MXM_COM_ERROR_REPLY_FAIL);
    }
}

void MxmCommunication::RemoveChannel(const std::string &engineName, const std::string &remoteNodeId,
                                     MxmChannelType type)
{
    auto engine = MxmComEngineManager::GetEngine(engineName);
    if (engine == nullptr) {
        DBG_LOGWARN("Get engine failed, engineName: " << engineName);
        return;
    }
    engine->RemoveChannel(remoteNodeId, type);
}

int MxmCommunication::SetPostReconnectHandler(const std::string &name, MxmComPostReconnectHandler handler)
{
    auto engine = MxmComEngineManager::GetEngine(name);
    if (engine == nullptr) {
        DBG_LOGERROR("Get engine failed, name: " << name);
        return HFAIL;
    }
    return engine->SetMxmComPostReconnectHandler(std::move(handler));
}
} // namespace ock::com
