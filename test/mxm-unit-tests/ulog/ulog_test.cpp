/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>

#include "log.h"
#include "logger/ubsmem_logger_manager.h"
#include "logger/ubsmem_logger_writer.h"

using namespace ubsmem::log;

namespace UT {
TEST(ulog, test_stdout)
{
    UbsmemLoggerDefaultWriter writer;
    UbsmemLoggerOptions opts;
    opts.bufferMaxItem = 4096;

    auto mgr = UbsmemLoggerManager::Instance();
    ASSERT_NE(mgr, nullptr);
    int ret = mgr->Init(opts, std::shared_ptr<UbsmemLoggerWriter>(&writer, [](auto *) {}));
    ASSERT_EQ(ret, 0);
    mgr->SetLogLevel(UbsmemLogLevel::DEBUG);

    DBG_LOGINFO("test stdout INFO");
    DBG_LOGDEBUG("test stdout DEBUG");
    DBG_LOGERROR("test stdout ERROR");

    std::this_thread::sleep_for(std::chrono::seconds(1));
    mgr->Exit();
    UbsmemLoggerManager::Destroy();
}

TEST(ulog, test_file)
{
    const char *path = "/tmp/ulog_test";
    UbsmemLoggerFilesink sink(path, 20 * 1024 * 1024, 10);
    sink.Initialize();

    UbsmemLoggerOptions opts;
    opts.bufferMaxItem = 4096;

    auto mgr = UbsmemLoggerManager::Instance();
    ASSERT_NE(mgr, nullptr);
    int ret = mgr->Init(opts, std::shared_ptr<UbsmemLoggerWriter>(&sink, [](auto *) {}));
    ASSERT_EQ(ret, 0);
    mgr->SetLogLevel(UbsmemLogLevel::DEBUG);

    DBG_LOGINFO("test file INFO");
    DBG_LOGERROR("test file ERROR");

    std::this_thread::sleep_for(std::chrono::seconds(1));
    mgr->Exit();
    UbsmemLoggerManager::Destroy();
}
}  // namespace UT