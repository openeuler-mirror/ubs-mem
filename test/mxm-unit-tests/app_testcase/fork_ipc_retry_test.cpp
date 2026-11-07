/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "log.h"
#include "mxm_msg.h"
#define private public
#include "ipc_proxy.h"
#undef private
#include "shm_ipc_command.h"

class ForkExistingSuspendTest : public testing::Test {
protected:
    ock::mxmd::IpcSuspendState previousState;
    void SetUp() override
    {
        previousState = ock::mxmd::IpcProxy::state_;
        ock::mxmd::IpcProxy::state_ = ock::mxmd::IpcSuspendState::RUNNING;
    }
    void TearDown() override
    {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
        ock::mxmd::IpcProxy::state_ = previousState;
    }
};

TEST_F(ForkExistingSuspendTest, PrepareChecksLocalStateWithoutSendingControlMessages)
{
    using namespace ock::mxmd;
    MOCKER(MxmComIpcClientExclusive).stubs().will(returnValue(true));
    MOCKER(ShmIpcCommand::IpcCallSuspendInner).expects(never());
    MOCKER(ShmIpcCommand::IpcCallResumeInner).expects(never());
    MOCKER(MxmComStopIpcClientForFork).expects(never());
    EXPECT_EQ(IpcProxy::PrepareFork(), 0U);
    EXPECT_EQ(IpcProxy::state_, IpcSuspendState::RUNNING);
}

TEST_F(ForkExistingSuspendTest, SuspendRetryUsesExistingIdempotentRequest)
{
    using namespace ock::mxmd;
    MOCKER(MxmComIpcClientExclusive).stubs().will(returnValue(true));
    MOCKER(ShmIpcCommand::IpcCallSuspendInner)
        .expects(exactly(2))
        .will(returnValue(static_cast<uint32_t>(ock::common::MXM_ERR_IPC_SYNC_CALL)))
        .then(returnValue(static_cast<uint32_t>(0)));
    MOCKER(MxmComStopIpcClientForFork).expects(once()).will(returnValue(0));
    MOCKER(MxmComStopIpcClient).expects(never());
    EXPECT_NE(IpcProxy::SuspendForFork(), 0U);
    EXPECT_EQ(IpcProxy::state_, IpcSuspendState::FAILED_SUSPENDING);
    EXPECT_EQ(IpcProxy::SuspendForFork(), 0U);
    EXPECT_EQ(IpcProxy::state_, IpcSuspendState::SUSPENDED);
}

TEST_F(ForkExistingSuspendTest, ParentRecoveryRetriesStopWithoutRepeatingAcknowledgedSuspend)
{
    using namespace ock::mxmd;
    MOCKER(MxmComIpcClientExclusive).stubs().will(returnValue(true));
    MOCKER(ShmIpcCommand::IpcCallSuspendInner).expects(once()).will(returnValue(static_cast<uint32_t>(0)));
    MOCKER(MxmComStopIpcClientForFork).expects(once()).will(returnValue(-1));
    MOCKER(MxmComStopIpcClient).expects(once()).will(returnValue(0));
    MOCKER(MxmComStartIpcClient).expects(once()).will(returnValue(0));
    MOCKER(MxmSetPostReconnectHandler).expects(once()).will(returnValue(0));
    MOCKER(ShmIpcCommand::IpcCallResumeInner).expects(once()).will(returnValue(static_cast<uint32_t>(0)));
    EXPECT_NE(IpcProxy::SuspendForFork(), 0U);
    EXPECT_EQ(IpcProxy::state_, IpcSuspendState::FAILED_STOPPING);
    EXPECT_EQ(IpcProxy::Resume(), 0U);
    EXPECT_EQ(IpcProxy::state_, IpcSuspendState::RUNNING);
}

TEST_F(ForkExistingSuspendTest, ResumeRetryReusesConnectionAndExistingRequest)
{
    using namespace ock::mxmd;
    IpcProxy::state_ = IpcSuspendState::SUSPENDED;
    MOCKER(MxmComStartIpcClient).expects(once()).will(returnValue(0));
    MOCKER(MxmSetPostReconnectHandler).expects(once()).will(returnValue(0));
    MOCKER(MxmComStopIpcClient).expects(never());
    MOCKER(ShmIpcCommand::IpcCallResumeInner)
        .expects(exactly(2))
        .will(returnValue(static_cast<uint32_t>(ock::common::MXM_ERR_IPC_SYNC_CALL)))
        .then(returnValue(static_cast<uint32_t>(0)));
    EXPECT_NE(IpcProxy::Resume(), 0U);
    EXPECT_EQ(IpcProxy::state_, IpcSuspendState::FAILED_RESUMING);
    EXPECT_EQ(IpcProxy::Resume(), 0U);
    EXPECT_EQ(IpcProxy::state_, IpcSuspendState::RUNNING);
    EXPECT_EQ(IpcProxy::Resume(), 0U);
}

TEST_F(ForkExistingSuspendTest, ChildStartsNewConnectionWithoutResumingParent)
{
    using namespace ock::mxmd;
    IpcProxy::state_ = IpcSuspendState::SUSPENDED;
    MOCKER(MxmComStartIpcClient).expects(once()).will(returnValue(0));
    MOCKER(MxmSetPostReconnectHandler).expects(once()).will(returnValue(0));
    MOCKER(ShmIpcCommand::IpcCallResumeInner).expects(never());
    EXPECT_EQ(IpcProxy::ForkChildInitialize(), 0U);
    EXPECT_EQ(IpcProxy::state_, IpcSuspendState::RUNNING);
}
