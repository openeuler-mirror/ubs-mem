/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <common_def.h>
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "mxm_msg.h"

namespace UT {
using namespace ock::mxmd;

constexpr auto TEST_DEFAULT_REGION_NAME = "default";
constexpr auto TEST_SHM_NAME = "TEST_SHM_NAME";
constexpr auto TEST_LEASE_NAME = "TEST_LEASE_NAME";
constexpr size_t TEST_SHM_SIZE = 1 << 30;
constexpr auto TEST_SHM_MODE = 0600;
constexpr auto TEST_SHM_FLAG = 0;

using TEST_MsgBaseFunc = std::function<MsgBase *()>;
static std::unordered_map<int16_t, TEST_MsgBaseFunc> gTestRequestMap = {
    {MXM_MSG_SHM_ALLOCATE, []() { return new(std::nothrow) ShmemAllocateRequest(); }},
    {MXM_MSG_SHM_DEALLOCATE, []() { return new(std::nothrow) ShmemDeallocateRequest(); }},
    {IPC_MALLOC_MEMORY, []() { return new(std::nothrow) AppMallocMemoryRequest(); }},
    {IPC_FREE_RACKMEM, []() { return new(std::nothrow) AppFreeMemoryRequest(); }},
    {IPC_QUERY_CLUSTERINFO, []() { return new(std::nothrow) CommonRequest(); }},
    {IPC_RACKMEMSHM_LOOKUP_SHAREREGIONS, []() { return new(std::nothrow) ShmLookRegionListRequest(); }},
    {IPC_RACKMEMSHM_CREATE, []() { return new(std::nothrow) ShmCreateRequest(); }},
    {IPC_RACKMEMSHM_DELETE, []() { return new(std::nothrow) ShmDeleteRequest(); }},
    {IPC_RACKMEMSHM_MMAP, []() { return new(std::nothrow) ShmMapRequest(); }},
    {IPC_RACKMEMSHM_UNMMAP, []() { return new(std::nothrow) ShmUnmapRequest(); }},
    {IPC_RACKMEMSHM_QUERY_MEM_FAULT_STATUS, []() { return new(std::nothrow) ShmQueryMemFaultStatusRequest(); }},
    {IPC_RACKMEMSHM_WRITELOCK, []() { return new(std::nothrow) ShmWriteLockRequest(); }},
    {IPC_RACKMEMSHM_READLOCK, []() { return new(std::nothrow) ShmReadLockRequest(); }},
    {IPC_RACKMEMSHM_UNLOCK, []() { return new(std::nothrow) ShmUnLockRequest(); }},
    {IPC_RACKMEMSHM_ATTACH, []() { return new(std::nothrow) ShmAttachRequest(); }},
    {IPC_RACKMEMSHM_DETACH, []() { return new(std::nothrow) ShmDetachRequest(); }},
    {IPC_RACKMEMSHM_LOOKUP_LIST, []() { return new(std::nothrow) ShmListLookupRequest(); }},
    {IPC_RACKMEMSHM_LOOKUP, []() { return new(std::nothrow) ShmLookupRequest(); }},
    {RPC_AGENT_QUERY_NODE_INFO, []() { return new(std::nothrow) CommonRequest(); }},
    {RPC_DLOCK_CLIENT_REINIT, []() { return new(std::nothrow) DLockClientReinitRequest(); }},
    {IPC_RACKMEMSHM_QUERY_NODE, []() { return new(std::nothrow) QueryNodeRequest(); }},
    {IPC_RACKMEMSHM_QUERY_DLOCK_STATUS, []() { return new(std::nothrow) CommonRequest(); }}
};
static std::unordered_map<int16_t, TEST_MsgBaseFunc> gTestResponseMap = {
    {MXM_MSG_SHM_ALLOCATE, []() { return new(std::nothrow) CommonResponse(); }},
    {MXM_MSG_SHM_DEALLOCATE, []() { return new(std::nothrow) CommonResponse(); }},
    {IPC_MALLOC_MEMORY, []() { return new(std::nothrow) AppMallocMemoryResponse(); }},
    {IPC_FREE_RACKMEM, []() { return new(std::nothrow) CommonResponse(); }},
    {IPC_QUERY_CLUSTERINFO, []() { return new(std::nothrow) AppQueryClusterInfoResponse(); }},
    {IPC_RACKMEMSHM_LOOKUP_SHAREREGIONS, []() { return new(std::nothrow) ShmLookRegionListResponse(); }},
    {IPC_RACKMEMSHM_CREATE, []() { return new(std::nothrow) CommonResponse(); }},
    {IPC_RACKMEMSHM_DELETE, []() { return new(std::nothrow) CommonResponse(); }},
    {IPC_RACKMEMSHM_MMAP, []() { return new(std::nothrow) ShmMapResponse(); }},
    {IPC_RACKMEMSHM_UNMMAP, []() { return new(std::nothrow) CommonResponse(); }},
    {IPC_RACKMEMSHM_QUERY_MEM_FAULT_STATUS, []() { return new(std::nothrow) ShmQueryMemFaultStatusResponse(); }},
    {IPC_RACKMEMSHM_WRITELOCK, []() { return new(std::nothrow) ShmWriteLockResponse(); }},
    {IPC_RACKMEMSHM_READLOCK, []() { return new(std::nothrow) ShmReadLockResponse(); }},
    {IPC_RACKMEMSHM_UNLOCK, []() { return new(std::nothrow) ShmUnLockResponse(); }},
    {IPC_RACKMEMSHM_ATTACH, []() { return new(std::nothrow) ShmAttachResponse(); }},
    {IPC_RACKMEMSHM_DETACH, []() { return new(std::nothrow) CommonResponse(); }},
    {IPC_RACKMEMSHM_LOOKUP_LIST, []() { return new(std::nothrow) ShmListLookupResponse(); }},
    {IPC_RACKMEMSHM_LOOKUP, []() { return new(std::nothrow) ShmLookupResponse(); }},
    {RPC_AGENT_QUERY_NODE_INFO, []() { return new(std::nothrow) RpcQueryInfoResponse(); }},
    {RPC_DLOCK_CLIENT_REINIT, []() { return new(std::nothrow) DLockClientReinitResponse(); }},
    {IPC_RACKMEMSHM_QUERY_NODE, []() { return new(std::nothrow) QueryNodeResponse(); }},
    {IPC_RACKMEMSHM_QUERY_DLOCK_STATUS, []() { return new(std::nothrow) QueryDlockStatusResponse(); }}
};


class MxmMessageTest : public testing::Test {
public:
    static void TearDownTestCase()
    {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }

    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

template <typename TMsg>
bool TestSerializeDeserialize()
{
    TMsg message{};
    NetMsgPacker packer;
    auto ret = message.Serialize(packer);
    if (ret != UBSM_OK) {
        return false;
    }
    NetMsgUnpacker unpacker(packer.String());

    TMsg messageNew;
    ret = messageNew.Deserialize(unpacker);
    if (ret != UBSM_OK) {
        return false;
    }
    return true; // 检查是否一致
}


TEST_F(MxmMessageTest, TestSerialize_ShmemAllocateRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmemAllocateRequest>();
    EXPECT_EQ(ret, true);
}


TEST_F(MxmMessageTest, TestSerialize_ShmemDeallocateRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmemDeallocateRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_AppMallocMemoryRequest_Success)
{
    auto ret = TestSerializeDeserialize<AppMallocMemoryRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_AppFreeMemoryRequest_Success)
{
    auto ret = TestSerializeDeserialize<AppFreeMemoryRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_AppMallocMemoryResponse_Success)
{
    auto ret = TestSerializeDeserialize<AppMallocMemoryResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_AppQueryClusterInfoResponse_Success)
{
    auto ret = TestSerializeDeserialize<AppQueryClusterInfoResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_AppQueryCachedMemoryResponse_Success)
{
    auto ret = TestSerializeDeserialize<AppQueryCachedMemoryResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmLookRegionListRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmLookRegionListRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmCreateRegionRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmCreateRegionRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmLookupRegionRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmLookupRegionRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmDestroyRegionRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmDestroyRegionRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmCreateRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmCreateRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmDeleteRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmDeleteRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmMapRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmMapRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmUnmapRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmUnmapRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmQueryMemFaultStatusRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmQueryMemFaultStatusRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_PingRequest_Success)
{
    auto ret = TestSerializeDeserialize<PingRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_VoteRequest_Success)
{
    auto ret = TestSerializeDeserialize<VoteRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_BroadcastRequest_Success)
{
    auto ret = TestSerializeDeserialize<BroadcastRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_TransElectedRequest_Success)
{
    auto ret = TestSerializeDeserialize<TransElectedRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmLookRegionListResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmLookRegionListResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmCreateRegionResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmCreateRegionResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmLookupRegionResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmLookupRegionResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmDestroyRegionResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmDestroyRegionResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmMapResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmMapResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmQueryMemFaultStatusResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmQueryMemFaultStatusResponse>();
    EXPECT_EQ(ret, true);
}


TEST_F(MxmMessageTest, TestSerialize_ShmWriteLockRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmWriteLockRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmWriteLockResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmWriteLockResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmReadLockRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmReadLockRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmReadLockResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmReadLockResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmUnLockRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmUnLockRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmUnLockResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmUnLockResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_RpcQueryInfoResponse_Success)
{
    auto ret = TestSerializeDeserialize<RpcQueryInfoResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_DLockClientReinitRequest_Success)
{
    auto ret = TestSerializeDeserialize<DLockClientReinitRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_DLockClientReinitResponse_Success)
{
    auto ret = TestSerializeDeserialize<DLockClientReinitResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_LockRequest_Success)
{
    auto ret = TestSerializeDeserialize<LockRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_UnLockRequest_Success)
{
    auto ret = TestSerializeDeserialize<UnLockRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_DLockResponse_Success)
{
    auto ret = TestSerializeDeserialize<DLockResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_RpcJoinInfoResponse_Success)
{
    auto ret = TestSerializeDeserialize<RpcJoinInfoResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_RpcVoteInfoResponse_Success)
{
    auto ret = TestSerializeDeserialize<RpcVoteInfoResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmAttachRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmAttachRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmAttachResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmAttachResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmDetachRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmDetachRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmLookupRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmLookupRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmLookupResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmLookupResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_QueryNodeRequest_Success)
{
    auto ret = TestSerializeDeserialize<QueryNodeRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_QueryNodeResponse_Success)
{
    auto ret = TestSerializeDeserialize<QueryNodeResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_QueryDlockStatusResponse_Success)
{
    auto ret = TestSerializeDeserialize<QueryDlockStatusResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmListLookupRequest_Success)
{
    auto ret = TestSerializeDeserialize<ShmListLookupRequest>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestSerialize_ShmListLookupResponse_Success)
{
    auto ret = TestSerializeDeserialize<ShmListLookupResponse>();
    EXPECT_EQ(ret, true);
}

TEST_F(MxmMessageTest, TestCreateRequestByOpCode_Success)
{
    for (const auto& it : gTestRequestMap) {
        auto ret = CreateRequestByOpCode(it.first);
        EXPECT_NE(ret, nullptr);
        SafeDelete(ret);
    }
}

TEST_F(MxmMessageTest, CreateResponseByOpCode_Success)
{
    for (const auto& it : gTestResponseMap) {
        auto ret = CreateResponseByOpCode(it.first);
        EXPECT_NE(ret, nullptr);
        SafeDelete(ret);
    }
}

TEST_F(MxmMessageTest, TestCreateRequestByOpCodeInner_Success)
{
    std::vector<int16_t> ops{IPC_REGION_LOOKUP_REGION_LIST, IPC_REGION_CREATE_REGION, IPC_REGION_LOOKUP_REGION,
                             IPC_REGION_DESTROY_REGION, IPC_FORCE_FREE_CACHED_MEMORY, IPC_QUERY_CACHED_MEMORY,
                             RPC_PING_NODE_INFO, RPC_JOIN_NODE_INFO, RPC_MASTER_ELECTED_NODE_INFO,
                             RPC_SEND_ELECTED_MASTER_INFO, RPC_VOTE_NODE_INFO, RPC_BROADCAST_NODE_INFO, RPC_LOCK,
                             RPC_UNLOCK};
    for (const auto& it : ops) {
        auto ret = CreateRequestByOpCodeInner(it);
        EXPECT_NE(ret, nullptr);
        SafeDelete(ret);
    }
}


TEST_F(MxmMessageTest, CreateResponseByOpCodeInner_Success)
{
    std::vector<int16_t> ops{IPC_REGION_LOOKUP_REGION_LIST, IPC_REGION_CREATE_REGION, IPC_REGION_LOOKUP_REGION,
                             IPC_REGION_DESTROY_REGION, IPC_FORCE_FREE_CACHED_MEMORY, IPC_QUERY_CACHED_MEMORY,
                             RPC_PING_NODE_INFO, RPC_JOIN_NODE_INFO, RPC_MASTER_ELECTED_NODE_INFO,
                             RPC_SEND_ELECTED_MASTER_INFO, RPC_VOTE_NODE_INFO, RPC_BROADCAST_NODE_INFO, RPC_LOCK,
                             RPC_UNLOCK};
    for (const auto& it : ops) {
        auto ret = CreateResponseByOpCodeInner(it);
        EXPECT_NE(ret, nullptr);
        SafeDelete(ret);
    }
}
}