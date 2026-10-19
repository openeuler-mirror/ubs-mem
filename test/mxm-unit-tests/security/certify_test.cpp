/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-mem is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "openssl_dl.h"
#include "ubs_certify_handler.h"
#include "ubs_common_config.h"

namespace UT {
using namespace ock::ubsm;

void EmptyCleanup() {}

class CertifyTest : public testing::Test {
protected:
    void TearDown() override
    {
        UbsCertifyHandler::GetInstance().StopScheduledCertVerify();
        UbsCommonConfig::GetInstance().SetTlsSwitch(false);
        mockcpp::GlobalMockObject::reset();
    }
};

TEST_F(CertifyTest, ScheduledVerifyCanStopImmediatelyAndRestart)
{
    UbsCommonConfig::GetInstance().SetTlsSwitch(true);
    MOCKER(VerifyCertificate).stubs().will(returnValue(0));
    MOCKER(CleanupOpensslDl).stubs().will(invoke(EmptyCleanup));
    auto &handler = UbsCertifyHandler::GetInstance();

    ASSERT_EQ(handler.StartScheduledCertVerify(), 0);
    handler.StopScheduledCertVerify();
    ASSERT_EQ(handler.StartScheduledCertVerify(), 0);
    handler.StopScheduledCertVerify();
}
} // namespace UT
