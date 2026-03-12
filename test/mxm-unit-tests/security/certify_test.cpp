/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <string>
#include <mockcpp/mokc.h>
#include <gtest/gtest.h>
#include "ubs_certify_handler.cpp"
#include "ubs_certify_handler.h"

using namespace ock::ubsm;

namespace UT {

class CertifyTest : public testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
};

TEST_F(CertifyTest, cert_check_success)
{
    std::string caPath = "test_certs/ca/ca.pem";
    std::string certPath = "test_certs/certs/cert1.pem";
    std::string crlPath = "test_certs/crl/ca.crl.pem";
    UbsCommonConfig::GetInstance().SetCaPath(caPath);
    UbsCommonConfig::GetInstance().SetCertPath(certPath);
    UbsCommonConfig::GetInstance().SetCrlPath(crlPath);
    auto ret = DoVerify();
    EXPECT_EQ(ret, 0);
}

TEST_F(CertifyTest, cert_check_revoked)
{
    std::string caPath = "test_certs/ca/ca.pem";
    std::string certPath = "test_certs/certs/cert2.pem";
    std::string crlPath = "test_certs/crl/ca.crl.pem";
    UbsCommonConfig::GetInstance().SetCaPath(caPath);
    UbsCommonConfig::GetInstance().SetCertPath(certPath);
    UbsCommonConfig::GetInstance().SetCrlPath(crlPath);
    auto ret = DoVerify();
    EXPECT_NE(ret, 0);
    UbsCommonConfig::GetInstance().SetCrlPath("");
    ret = DoVerify();
    EXPECT_EQ(ret, 0);
}

TEST_F(CertifyTest, cert_check_expired)
{
    std::string caPath = "test_certs/ca/ca.pem";
    std::string certPath = "test_certs/certs/cert3.pem";
    std::string crlPath = "test_certs/crl/ca.crl.pem";
    UbsCommonConfig::GetInstance().SetCaPath(caPath);
    UbsCommonConfig::GetInstance().SetCertPath(certPath);
    UbsCommonConfig::GetInstance().SetCrlPath(crlPath);
    auto ret = DoVerify();
    EXPECT_NE(ret, 0);
}

TEST_F(CertifyTest, cert_schedule_task)
{
    std::string caPath = "test_certs/ca/ca.pem";
    std::string certPath = "test_certs/certs/cert1.pem";
    std::string crlPath = "test_certs/crl/ca.crl.pem";
    UbsCommonConfig::GetInstance().SetCaPath(caPath);
    UbsCommonConfig::GetInstance().SetCertPath(certPath);
    UbsCommonConfig::GetInstance().SetCrlPath(crlPath);
    UbsCommonConfig::GetInstance().SetTlsSwitch(true);
    auto ret = UbsCertifyHandler::GetInstance().StartScheduledCertVerify();
    EXPECT_EQ(ret, 0);
    UbsCertifyHandler::GetInstance().StopScheduledCertVerify();
    UbsCommonConfig::GetInstance().SetTlsSwitch(false);
}

}