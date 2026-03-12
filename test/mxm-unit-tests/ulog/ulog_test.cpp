/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include <gtest/gtest.h>

#include "log.h"
#include "ulog4c.h"
static void ClearUlog()
{
    // 清除Ulogger示例
    spdlog::drop_all();
    if (ock::utilities::log::ULog::gLogger != nullptr) {
        delete ock::utilities::log::ULog::gLogger;
        ock::utilities::log::ULog::gLogger = nullptr;
    }
    if (ock::utilities::log::ULog::gAuditLogger != nullptr) {
        delete ock::utilities::log::ULog::gAuditLogger;
        ock::utilities::log::ULog::gAuditLogger = nullptr;
    }
}

namespace UT {
TEST(ulog, test_printfLog)
{
    int ret = ock::utilities::log::ULog::CreateInstance(1, 1, "/tmp/ulog_test.log", 20971520, 20);
    EXPECT_EQ(0, ret);

    int a = 11;
    DBG_LOGINFO("test INFO");
    DBG_LOGINFO("test INFO {}", a);

    char c = 'y';
    DBG_LOGWARN("test WARN {}", c);
    DBG_LOGWARN("test WARN ");

    DBG_LOGDEBUG("test debug?{}", c);
    DBG_LOGDEBUG("test debug?");

    DBG_LOGERROR("test error?{}", c);
    DBG_LOGERROR("test error?");

    const char *prefix = "test";
    const char *message = "LogMessage";
    ret = ock::utilities::log::ULog::LogMessage(1, prefix, message);
    EXPECT_EQ(0, ret);

    const char *pattern = "%Y-%m-%d %H:%M:%S.%e %l : %v";
    ret = ock::utilities::log::ULog::SetInstancePattern(pattern);
    EXPECT_EQ(0, ret);

    ock::utilities::log::ULog::Flush();
}

TEST(ulog, test_limit)
{
    int ret = ock::utilities::log::ULog::CreateInstance(1, 1, "/tmp/ulog_test.log", 20971520, 20);
    EXPECT_EQ(0, ret);
    DBG_LOGERROR_LIMIT("test limit log", 1, 1);
}

TEST(ulog, test_printfAuditLog)
{
    int ret = ock::utilities::log::ULog::CreateInstanceAudit("/tmp/ulog_test_audit.log", 20971520, 20);
    EXPECT_EQ(0, ret);

    const char *pre = "test";
    const char *mess = "LogAuditMessage";
    ret = ock::utilities::log::ULog::LogAuditMessage(1, pre, mess);
    EXPECT_EQ(0, ret);
}

TEST(ulog, test_error)
{
    ClearUlog();
    int ret = 0;
    const char *prefix = "test";
    const char *message = "LogMessage";
    ret = ock::utilities::log::ULog::LogMessage(1, prefix, message);
    EXPECT_EQ(ret, 3);

    const char *pattern = "%Y-%m-%d %H:%M:%S.%e %l : %v";
    ret = ock::utilities::log::ULog::SetInstancePattern(pattern);
    EXPECT_EQ(ret, 3);

    DBG_LOGERROR(message);
    DBG_LOGWARN(message);
    DBG_LOGINFO(message);
    DBG_LOGDEBUG(message);

    const char *null = nullptr;
    ret = ock::utilities::log::ULog::LogMessage(1, null, message);
    EXPECT_EQ(ret, 2);
    ret = ock::utilities::log::ULog::SetInstancePattern(null);
    EXPECT_EQ(ret, 2);
}

TEST(ulog, test_auditlog)
{
    int ret = ULOG_AuditLogMessageF(nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(-1, ret);
    ret = ULOG_AuditLogMessageF("1", "2", "3", "4");
    EXPECT_EQ(-1, ret);
}
}  // namespace UT