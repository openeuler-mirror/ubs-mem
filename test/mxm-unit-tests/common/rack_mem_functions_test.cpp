/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <dlfcn.h>

#include "rack_mem_functions.h"

using testing::Test;

namespace UT {
using namespace ock::mxmd;

class RackMemFunctionsTestSuite : public Test {
public:
protected:
    void SetUp() override
    {}

    void TearDown() override
    {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }
};

class Task : public Runnable {
public:
    void Run() override
    {
        std::cout << "task is executed" << std::endl;
    }
};

TEST_F(RackMemFunctionsTestSuite, TestGetThreadExecutorService)
{
    ASSERT_NE(GetOneThreadExecutorService(), nullptr);
    ASSERT_NE(GetMoreThreadExecutorService(), nullptr);
}

TEST_F(RackMemFunctionsTestSuite, TestMemStrUtil)
{
    uint64_t value = 0;
    ASSERT_TRUE(MemStrUtil::StrToULL("0x0", value, 16U));
    ASSERT_EQ(value, 0);
    ASSERT_TRUE(MemStrUtil::StrToULL("0x00001000", value, 16U));
    ASSERT_EQ(value, 4096);
    ASSERT_FALSE(MemStrUtil::StrToULL("xxxx", value, 16U));

    std::vector<std::string> out = MemStrUtil::SplitTrim("Test|Mem||Str|Util", "|");
    ASSERT_EQ(out.size(), (std::size_t)4);
}
}  // namespace UT
