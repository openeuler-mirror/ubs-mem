#include <gtest/gtest.h>
#include "ubs_mem.h"
#include "rack_mem_functions.h"
#include "rack_mem_lib.h"
#define private public
#include "ipc_proxy.h"
#undef private

using namespace ock::common;
using namespace ock::mxmd;

TEST(RackMemLibCApiTest, InitAttributesNull)
{
    int ret = ubsmem_init_attributes(nullptr);
    EXPECT_EQ(ret, UBSM_ERR_PARAM_INVALID);
}

TEST(RackMemLibCApiTest, InitAttributesNonNull)
{
    ubsmem_options_t opts;
    int ret = ubsmem_init_attributes(&opts);
    EXPECT_EQ(ret, UBSM_OK);
}

TEST(RackMemLibCApiTest, SetLoggerLevelInvalid)
{
    int ret = ubsmem_set_logger_level(-1);
    EXPECT_EQ(ret, UBSM_ERR_PARAM_INVALID);
    ret = ubsmem_set_logger_level(100);
    EXPECT_EQ(ret, UBSM_ERR_PARAM_INVALID);
}

TEST(RackMemLibCApiTest, SetExternLoggerNull)
{
    int ret = ubsmem_set_extern_logger(nullptr);
    EXPECT_EQ(ret, UBSM_ERR_PARAM_INVALID);
}

TEST(RackMemLibCApiTest, SuspendedControlOperationReturnsDedicatedError)
{
    const auto previousState = IpcProxy::state_;
    IpcProxy::state_ = IpcSuspendState::SUSPENDED;
    bool called = false;

    const auto ret = RackMemLib::GetInstance().RunIpcOperation([&called]() {
        called = true;
        return static_cast<uint32_t>(UBSM_OK);
    });

    IpcProxy::state_ = previousState;
    EXPECT_EQ(ret, MXM_ERR_IPC_SUSPENDED);
    EXPECT_EQ(GetErrCode(ret), UBSM_ERR_IPC_SUSPENDED);
    EXPECT_FALSE(called);
}

TEST(RackMemLibCApiTest, TransitioningControlOperationReturnsBusy)
{
    const auto previousState = IpcProxy::state_;
    IpcProxy::state_ = IpcSuspendState::SUSPENDING;

    const auto ret = RackMemLib::GetInstance().RunIpcOperation([]() { return static_cast<uint32_t>(UBSM_OK); });

    IpcProxy::state_ = previousState;
    EXPECT_EQ(ret, MXM_ERR_NAME_BUSY);
    EXPECT_EQ(GetErrCode(ret), UBSM_ERR_BUSY);
}

TEST(RackMemLibCApiTest, SetOwnershipIsAllowedWhileIpcSuspended)
{
    const auto previousState = IpcProxy::state_;
    IpcProxy::state_ = IpcSuspendState::SUSPENDED;

    const auto ret = ubsmem_shmem_set_ownership(nullptr, nullptr, 0, 0);

    IpcProxy::state_ = previousState;
    EXPECT_EQ(ret, UBSM_ERR_PARAM_INVALID);
}

TEST(RackMemLibCApiTest, ControlOperationDoesNotHoldLifecycleLock)
{
    const auto previousState = IpcProxy::state_;
    IpcProxy::state_ = IpcSuspendState::RUNNING;

    const auto ret = RackMemLib::GetInstance().RunIpcOperation([]() {
        const bool lockAvailable = IpcProxy::stateLock_.try_lock();
        if (lockAvailable) {
            IpcProxy::stateLock_.unlock();
        }
        EXPECT_TRUE(lockAvailable);
        return static_cast<uint32_t>(UBSM_OK);
    });

    IpcProxy::state_ = previousState;
    EXPECT_EQ(ret, UBSM_OK);
    EXPECT_EQ(IpcProxy::activeOperations_, 0);
}
