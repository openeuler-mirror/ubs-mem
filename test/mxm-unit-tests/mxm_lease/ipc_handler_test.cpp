/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "ipc_handler.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <mockcpp/mockcpp.hpp>
#include "mls_manager.h"
#include "ubse_mem_adapter_stub.h"

namespace UT {
using namespace ock::lease::service;
using namespace ock::mxm;
#ifndef MOCKER_CPP
#define MOCKER_CPP(api, TT) (MOCKCPP_NS::mockAPI((#api), (reinterpret_cast<TT>(api))))
#endif

class MLSIpcHandlerTest : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override
    {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }
};

TEST_F(MLSIpcHandlerTest, TestAppMallocMemoryReuseBufferedMemReturnZero)
{
    auto request = std::make_shared<AppMallocMemoryRequest>(4096, 0, false, "default");
    auto response = std::make_shared<AppMallocMemoryResponse>();
    MxmComUdsInfo udsInfo{};
    MOCKER_CPP(&MLSManager::ReuseBufferedMem, int32_t(*)(uint64_t size, uint16_t isNuma, const std::string &regionName,
                                                         const AppContext &context, MLSMemInfo &info))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&UbseMemAdapter::FdPermissionChange,
               int (*)(const std::string &name, const AppContext &appContext, mode_t mode))
        .stubs()
        .will(returnValue(0));

    auto result = MxmServerMsgHandle::AppMallocMemory(request.get(), response.get(), udsInfo);

    ASSERT_EQ(UBSM_OK, result);
}

TEST_F(MLSIpcHandlerTest, TestAppMallocMemoryReuseBufferedMemReturnNonZero)
{
    auto request = std::make_shared<AppMallocMemoryRequest>(4096, 0, false, "default");
    auto response = std::make_shared<AppMallocMemoryResponse>();
    MxmComUdsInfo udsInfo{};
    MOCKER_CPP(&MLSManager::ReuseBufferedMem, int32_t(*)(uint64_t size, uint16_t isNuma, const std::string &regionName,
                                                         const AppContext &context, MLSMemInfo &info))
        .stubs()
        .will(returnValue(-1));

    ock::ubsm::LeaseMallocResult res{.memIds = {1, 2, 3}, .numaId = 1, .unitSize = 8};
    MOCKER(ock::mxm::UbseMemAdapter::LeaseMalloc).stubs().with(any(), outBound(res)).will(returnValue(0));
    auto result = MxmServerMsgHandle::AppMallocMemory(request.get(), response.get(), udsInfo);

    ASSERT_EQ(UBSM_OK, result);
}

TEST_F(MLSIpcHandlerTest, TestAppMallocMemory_FailWhenParamNullptr)
{
    auto result = MxmServerMsgHandle::AppMallocMemory(nullptr, nullptr, {});

    ASSERT_NE(result, 0);
}

TEST_F(MLSIpcHandlerTest, TestAppMallocMemoryRejectsZeroSize)
{
    auto request = std::make_shared<AppMallocMemoryRequest>(0, 0, false, "default");
    auto response = std::make_shared<AppMallocMemoryResponse>();

    EXPECT_EQ(MxmServerMsgHandle::AppMallocMemory(request.get(), response.get(), {}), MXM_ERR_PARAM_INVALID);
    EXPECT_EQ(response->errCode_, MXM_ERR_PARAM_INVALID);
}

TEST_F(MLSIpcHandlerTest, TestAppMallocMemoryWithLocRejectsZeroSize)
{
    auto request = std::make_shared<AppMallocMemoryWithLocRequest>(0, false, 0, 0, 0, 0);
    auto response = std::make_shared<AppMallocMemoryResponse>();

    EXPECT_EQ(MxmServerMsgHandle::AppMallocMemoryWithLoc(request.get(), response.get(), {}), MXM_ERR_PARAM_INVALID);
    EXPECT_EQ(response->errCode_, MXM_ERR_PARAM_INVALID);
}

TEST_F(MLSIpcHandlerTest, Test_AppFreeMemory_SuccessWhenFdMalloc)
{
    std::vector<uint64_t> memIds{1, 2, 3, 4, 5, 6, 7, 8};
    auto name = "N111223344";
    MLSManager::GetInstance().PreAddUsedMem(name, 1 << 30, {}, false, 0);

    MLSManager::GetInstance().FinishAddUsedMem(name, -1, 1 << 27, 0, memIds);
    std::shared_ptr<AppFreeMemoryRequest> request = std::make_shared<AppFreeMemoryRequest>(memIds, "default", name);

    std::shared_ptr<CommonResponse> response = std::make_shared<CommonResponse>();

    MOCKER_CPP(&UbseMemAdapter::FdPermissionChange,
               int (*)(const std::string &name, const AppContext &appContext, mode_t mode))
        .stubs()
        .will(returnValue(0));

    MOCKER_CPP(&UbseMemAdapter::LeaseFree, int (*)(const std::string &name, bool isNuma)).stubs().will(returnValue(0));

    auto ret = MxmServerMsgHandle::AppFreeMemory(request.get(), response.get(), {});

    EXPECT_EQ(ret, 0);
}

TEST_F(MLSIpcHandlerTest, Test_AppFreeMemory_SuccessWhenNumaMalloc)
{
    std::vector<uint64_t> memIds{9, 10, 11, 12, 13, 14, 15, 16};
    auto name = "N443322111";

    MLSManager::GetInstance().PreAddUsedMem(name, 1 << 30, {}, false, 0);

    MLSManager::GetInstance().FinishAddUsedMem(name, -1, 1 << 27, 0, memIds);
    std::shared_ptr<AppFreeMemoryRequest> request = std::make_shared<AppFreeMemoryRequest>(memIds, "default", name);

    std::shared_ptr<CommonResponse> response = std::make_shared<CommonResponse>();

    MOCKER_CPP(&UbseMemAdapter::FdPermissionChange,
               int (*)(const std::string &name, const AppContext &appContext, mode_t mode))
        .stubs()
        .will(returnValue(0));

    MOCKER_CPP(&UbseMemAdapter::LeaseFree, int (*)(const std::string &name, bool isNuma)).stubs().will(returnValue(0));

    auto ret = MxmServerMsgHandle::AppFreeMemory(request.get(), response.get(), {});

    EXPECT_EQ(ret, 0);
}

TEST_F(MLSIpcHandlerTest, TestAppFreeMemoryRejectsNonOwner)
{
    const std::string name = "N_OWNER_CHECK";
    const AppContext owner{123, 456, 789};
    ASSERT_EQ(MLSManager::GetInstance().PreAddUsedMem(name, 4096, owner, true, 0), 0);
    ASSERT_EQ(MLSManager::GetInstance().FinishAddUsedMem(name, 0, 4096, 0, {0}), 0);
    auto request = std::make_shared<AppFreeMemoryRequest>(std::vector<uint64_t>{0}, "default", name);
    auto response = std::make_shared<CommonResponse>();

    EXPECT_EQ(MxmServerMsgHandle::AppFreeMemory(request.get(), response.get(), {124, 456, 789}),
              MXM_ERR_SHM_PERMISSION_DENIED);
    MLSManager::GetInstance().DeleteUsedMem(name);
}

TEST_F(MLSIpcHandlerTest, TestAppQueryClusterInfo_Success)
{
    std::shared_ptr<CommonRequest> request = std::make_shared<CommonRequest>();
    std::shared_ptr<AppQueryClusterInfoResponse> response = std::make_shared<AppQueryClusterInfoResponse>();
    MOCKER_CPP(&UbseMemAdapter::LookUpClusterStatistic, int (*)(ubsmemClusterInfo &clusterInfo))
        .stubs()
        .will(returnValue(0));

    auto ret = MxmServerMsgHandle::AppQueryClusterInfo(request.get(), response.get(), {});

    EXPECT_EQ(ret, 0);
}

TEST_F(MLSIpcHandlerTest, TestAppQueryClusterInfo_FailWhenUbseFail)
{
    std::shared_ptr<CommonRequest> request = std::make_shared<CommonRequest>();
    std::shared_ptr<AppQueryClusterInfoResponse> response = std::make_shared<AppQueryClusterInfoResponse>();
    MOCKER_CPP(&UbseMemAdapter::LookUpClusterStatistic, int (*)(ubsmemClusterInfo &clusterInfo))
        .stubs()
        .will(returnValue(-1));

    auto ret = MxmServerMsgHandle::AppQueryClusterInfo(request.get(), response.get(), {});

    EXPECT_EQ(ret, -1);
}

TEST_F(MLSIpcHandlerTest, TestAppQueryClusterInfo_FailWhenNullptr)
{
    auto ret = MxmServerMsgHandle::AppQueryClusterInfo(nullptr, nullptr, {});

    EXPECT_EQ(ret, MXM_ERR_NULLPTR);
}

TEST_F(MLSIpcHandlerTest, TestAppQueryCachedMemory_Success)
{
    std::shared_ptr<CommonRequest> request = std::make_shared<CommonRequest>();
    std::shared_ptr<AppQueryCachedMemoryResponse> response = std::make_shared<AppQueryCachedMemoryResponse>();

    auto ret = MxmServerMsgHandle::AppQueryCachedMemory(request.get(), response.get(), {});

    EXPECT_EQ(ret, UBSM_OK);
}

TEST_F(MLSIpcHandlerTest, TestAppForceFreeCachedMemory_Success)
{
    std::shared_ptr<CommonRequest> request = std::make_shared<CommonRequest>();
    std::shared_ptr<CommonResponse> response = std::make_shared<CommonResponse>();

    MOCKER_CPP(&UbseMemAdapter::LeaseFree, int (*)(const std::string &name, bool isNuma)).stubs().will(returnValue(0));

    auto ret = MxmServerMsgHandle::AppForceFreeCachedMemory(request.get(), response.get(), {});

    EXPECT_EQ(ret, UBSM_OK);
}

TEST_F(MLSIpcHandlerTest, TestAppForceFreeCachedMemory_FailWhenNullptr)
{
    auto ret = MxmServerMsgHandle::AppForceFreeCachedMemory(nullptr, nullptr, {});

    EXPECT_EQ(ret, MXM_ERR_NULLPTR);
}

TEST_F(MLSIpcHandlerTest, TestAppQueryCachedMemory_FailWhenNullptr)
{
    auto ret = MxmServerMsgHandle::AppQueryCachedMemory(nullptr, nullptr, {});

    EXPECT_EQ(ret, MXM_ERR_NULLPTR);
}

// MLSManager::ReuseBufferedMem
TEST_F(MLSIpcHandlerTest, TestReuseBufferedMem_SuccessWhenNumaBorrow)
{
    std::vector<uint64_t> memIds{1, 2, 3, 4, 5, 6, 7, 8};
    MLSMemInfo memory{"TEST_NUMA_BORROW", {1111, 1111, 1111}, 1 << 30, 4, memIds, 1 << 27, 0, 1};
    MLSManager::GetInstance().PreAddUsedMem("TEST_NUMA_BORROW", 1 << 30, {}, true, 0);
    MLSManager::GetInstance().FinishAddUsedMem("TEST_NUMA_BORROW", 4, 1 << 27, 0, memIds);
    MLSManager::GetInstance().PreDeleteUsedMem("TEST_NUMA_BORROW");
    std::vector<uint32_t> slotIds{0, 1};
    MOCKER(ock::mxm::UbseMemAdapter::GetRegionInfo).stubs().with(any(), outBound(slotIds)).will(returnValue(0));
    auto ret = MLSManager::GetInstance().ReuseBufferedMem(1 << 30, 1, "default", {}, memory);

    EXPECT_EQ(ret, 0);
}

TEST_F(MLSIpcHandlerTest, TestReuseBufferedMem_SuccessWhenFdBorrow)
{
    std::vector<uint64_t> memIds{9, 10, 11, 12, 13, 14, 15, 16};
    MLSMemInfo memory{"TEST_FD_BORROW", {1111, 1111, 1111}, 1 << 30, -1, memIds, 1 << 27, 0, 1};
    MLSManager::GetInstance().PreAddUsedMem("TEST_FD_BORROW", 1 << 30, {}, false, 0);
    MLSManager::GetInstance().FinishAddUsedMem("TEST_FD_BORROW", -1, 1 << 27, 0, memIds);
    MLSManager::GetInstance().PreDeleteUsedMem("TEST_FD_BORROW");
    std::vector<uint32_t> slotIds{0, 1};
    MOCKER(ock::mxm::UbseMemAdapter::GetRegionInfo).stubs().with(any(), outBound(slotIds)).will(returnValue(0));
    auto ret = MLSManager::GetInstance().ReuseBufferedMem(1 << 30, 0, "default", {}, memory);

    EXPECT_EQ(ret, 0);
}

TEST_F(MLSIpcHandlerTest, TestGetUsedMemByPid_Success)
{
    auto ret = MLSManager::GetInstance().GetUsedMemByPid(0);
    EXPECT_NE(ret.size(), 0);
}

TEST_F(MLSIpcHandlerTest, TestRecoveryDeletesUnfinishedBufferedRecord)
{
    MOCKER_CPP(&UbseMemAdapter::GetLeaseMemoryStage,
               int (*)(const std::string &name, bool isNuma, ubs_mem_stage &state))
        .stubs()
        .will(returnValue(static_cast<int>(MXM_ERR_LEASE_NOT_EXIST)));
    MemLeaseInfo stale;
    stale.first = {
        .fdMode = true, .name = "STALE_BUFFERED_LEASE", .size = 4096, .appContext = {}, .state = RecordState::PRE_DEL};
    stale.second = {.memIds = {1}, .numaId = -1, .unitSize = 4096, .slotId = 0};
    ASSERT_EQ(RecordStore::GetInstance().AddMemLeaseInput(stale.first), 0);
    ASSERT_EQ(RecordStore::GetInstance().AddMemLeaseResult(stale.first.name, stale.second), 0);
    ASSERT_EQ(RecordStore::GetInstance().UpdateMemLeaseRecordState(stale.first.name, RecordState::PRE_DEL), 0);

    EXPECT_EQ(MLSManager::GetInstance().RecoverFromRecord(), 0);
    const auto records = RecordStore::GetInstance().ListMemLeaseRecord();
    EXPECT_EQ(std::find_if(records.begin(), records.end(),
                           [](const MemLeaseInfo &record) { return record.first.name == "STALE_BUFFERED_LEASE"; }),
              records.end());
}
} // namespace UT
