/*
 Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */
#include "gtest/gtest.h"

#define private public
#include "ubse_mem_adapter.h"
#undef private

#include "ubs_mem_def.h"

using namespace ock::mxm;
namespace UT {
class CheckFeatureCompatibilityTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        UbseMemAdapter::ubFeatureValid_ = false;
        UbseMemAdapter::ubFeature_ = 0;
        UbseMemAdapter::ubFeatureLoaded_ = false;
    }
};

TEST_F(CheckFeatureCompatibilityTest, ubFeatureValidFalseSkipsValidation)
{
    UbseMemAdapter::ubFeatureValid_ = false;
    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_CACHE), MXM_OK);
    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_ONLY_IMPORT_NONCACHE), MXM_OK);
}

TEST_F(CheckFeatureCompatibilityTest, snoopOffRejectsNC_CC)
{
    UbseMemAdapter::ubFeatureValid_ = true;
    UbseMemAdapter::ubFeature_ = UBS_FEATURE_BIT_CC_CACHEABLE;

    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_ONLY_IMPORT_NONCACHE), MXM_ERR_UBSE_NOT_SUPPORTED);
}

TEST_F(CheckFeatureCompatibilityTest, snoopOffAllowsCCMode)
{
    UbseMemAdapter::ubFeatureValid_ = true;
    UbseMemAdapter::ubFeature_ = UBS_FEATURE_BIT_CC_CACHEABLE;

    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_CACHE), MXM_OK);
    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_WITH_LOCK), MXM_OK);
}

TEST_F(CheckFeatureCompatibilityTest, snoopOnRejectsCCMode)
{
    UbseMemAdapter::ubFeatureValid_ = true;
    UbseMemAdapter::ubFeature_ = 0;

    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_CACHE), MXM_ERR_UBSE_NOT_SUPPORTED);
    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_WITH_LOCK), MXM_ERR_UBSE_NOT_SUPPORTED);
}

TEST_F(CheckFeatureCompatibilityTest, snoopOnAllowsNC_CC)
{
    UbseMemAdapter::ubFeatureValid_ = true;
    UbseMemAdapter::ubFeature_ = 0;

    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_ONLY_IMPORT_NONCACHE), MXM_OK);
}

TEST_F(CheckFeatureCompatibilityTest, NC_NCModeNotChecked)
{
    UbseMemAdapter::ubFeatureValid_ = true;
    UbseMemAdapter::ubFeature_ = 0;

    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_NONCACHE), MXM_OK);
}

TEST_F(CheckFeatureCompatibilityTest, NC_NCModeNotCheckedSnoopOff)
{
    UbseMemAdapter::ubFeatureValid_ = true;
    UbseMemAdapter::ubFeature_ = UBS_FEATURE_BIT_CC_CACHEABLE;

    EXPECT_EQ(UbseMemAdapter::CheckFeatureCompatibility(UBSM_FLAG_NONCACHE), MXM_OK);
}
} // namespace UT
