/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <dlfcn.h>

#include "common/hdagger/logger/dg_out_logger.h"

using testing::Test;

namespace UT {
using namespace ock::dagger;

class DgOutLoggerTestSuite : public Test {
protected:
    void SetUp() override
    {}

    void TearDown() override
    {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }
};

TEST_F(DgOutLoggerTestSuite, TestLog)
{
    OutLogger::Instance()->SetLogLevel(DEBUG_LEVEL);
    DAGGER_LOG_DEBUG(test, "debug message");
    DAGGER_LOG_INFO(test, "info message");
    DAGGER_LOG_WARN(test, "warn message");
    DAGGER_LOG_ERROR(test, "error message");
    auto func = OutLogger::Instance()->GetExternalLogFunction();
    OutLogger::Instance()->SetExternalLogFunction(func, false);
}
}  // namespace UT
