/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
// Exercise the public-entry guard with production logging enabled.
#undef DEBUG_MEM_UT
#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#define private public
#include "fork_coordinator.h"
#include "logger/ubsmem_logger_manager.h"
#undef private

static int GuardedEntry()
{
    UBSMEM_API_GUARD();
    return UBSM_OK;
}

TEST(ForkAdmissionLoggingTest, RejectedEntryDoesNotAcquireLoggerMutex)
{
    auto &control = ock::mxmd::ForkCoordinator::Instance();
    control.admission_ = ock::mxmd::ForkCoordinator::CLOSED;
    std::unique_lock<std::mutex> loggerLock(ubsmem::log::UbsmemLoggerManager::gInitMutex_);
    std::promise<int> result;
    auto completion = result.get_future();
    std::thread rejected([&] { result.set_value(GuardedEntry()); });
    const auto ready = completion.wait_for(std::chrono::seconds(1));
    // Release before joining even if a regression causes the rejected call to log.
    loggerLock.unlock();
    rejected.join();
    control.admission_ = 0;
    EXPECT_EQ(ready, std::future_status::ready);
    EXPECT_EQ(completion.get(), UBSM_ERR_BUSY);
}
