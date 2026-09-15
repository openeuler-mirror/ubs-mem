/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 */
#include <dlfcn.h>
#include <gtest/gtest.h>
#include <mockcpp/mokc.h>
#include <securec.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <mockcpp/mockcpp.hpp>
#include <string>
#include <vector>
#include "system_adapter.h"
#include "ubs_cryptor_handler.h"

using namespace ock::ubsm;

namespace UT {

class CryptorTest : public testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
        GlobalMockObject::reset();
        for (const auto &path : temporaryPaths) {
            unlink(path.c_str());
        }
    }

    std::string CreateFile(const std::string &content)
    {
        char path[] = "/tmp/ubs_cryptor_test_XXXXXX";
        int fd = mkstemp(path);
        if (fd < 0) {
            return {};
        }
        close(fd);

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << content;
        output.close();
        temporaryPaths.emplace_back(path);
        return path;
    }

    std::vector<std::string> temporaryPaths;
};

static void TestLogger(int level, const char *str)
{
    std::cout << "[DECRYPT]"
              << "[" << level << "]" << str << std::endl;
}

static char *MockDecrypt(const char *encryptedData, size_t encryptedLen, size_t *outputLen)
{
    (void)encryptedData;
    (void)encryptedLen;
    (void)outputLen;
    return nullptr;
}

TEST_F(CryptorTest, DefaultDecryptReadsFile)
{
    auto path = CreateFile("secret\n");
    ASSERT_FALSE(path.empty());
    UbsCryptorHandler handler;
    std::pair<char *, int> result{nullptr, 0};

    ASSERT_EQ(handler.Decrypt(0, path, result), 0);
    ASSERT_NE(result.first, nullptr);
    EXPECT_EQ(result.second, 6);
    EXPECT_STREQ(result.first, "secret");

    handler.EraseDecryptData(result.first, result.second);
    handler.EraseDecryptData(nullptr, 0);
}

TEST_F(CryptorTest, DecryptRejectsMissingFile)
{
    auto path = CreateFile("unused");
    ASSERT_FALSE(path.empty());
    ASSERT_EQ(unlink(path.c_str()), 0);
    UbsCryptorHandler handler;
    std::pair<char *, int> result{nullptr, 0};

    EXPECT_EQ(handler.Decrypt(0, path, result), -1);
    EXPECT_EQ(result.first, nullptr);
}

TEST_F(CryptorTest, DecryptRejectsEmptyFile)
{
    auto path = CreateFile("");
    ASSERT_FALSE(path.empty());
    UbsCryptorHandler handler;
    std::pair<char *, int> result{nullptr, 0};

    EXPECT_EQ(handler.Decrypt(0, path, result), -1);
    EXPECT_EQ(result.first, nullptr);
}

TEST_F(CryptorTest, InitializeRejectsSymlinkedLibrary)
{
    auto target = CreateFile("not a library");
    ASSERT_FALSE(target.empty());
    auto link = target + ".so";
    ASSERT_EQ(symlink(target.c_str(), link.c_str()), 0);
    temporaryPaths.emplace_back(link);
    UbsCryptorHandler handler;
    handler.SetDecryptLibPath(link);

    EXPECT_EQ(handler.Initialize(), -1);
}

TEST_F(CryptorTest, InitializeReturnsErrorWhenDlopenFails)
{
    auto path = CreateFile("not a library");
    ASSERT_FALSE(path.empty());
    MOCKER(SystemAdapter::DlOpen)
        .expects(once())
        .with(checkWith([path](const char *value) { return path == value; }), eq(RTLD_NOW))
        .will(returnValue(static_cast<void *>(nullptr)));
    UbsCryptorHandler handler;
    handler.SetDecryptLibPath(path);

    EXPECT_EQ(handler.Initialize(), -1);
}

TEST_F(CryptorTest, InitializeClosesLibraryWhenSymbolIsMissing)
{
    auto path = CreateFile("not a library");
    ASSERT_FALSE(path.empty());
    int mockHandle = 0;
    MOCKER(SystemAdapter::DlOpen).expects(once()).will(returnValue(static_cast<void *>(&mockHandle)));
    MOCKER(SystemAdapter::DlSym).expects(once()).will(returnValue(static_cast<void *>(nullptr)));
    MOCKER(SystemAdapter::DlClose).expects(once()).with(eq(static_cast<void *>(&mockHandle))).will(returnValue(0));
    UbsCryptorHandler handler;
    handler.SetDecryptLibPath(path);

    EXPECT_EQ(handler.Initialize(), -1);
}

TEST_F(CryptorTest, InitializeOnlyLoadsLibraryOnce)
{
    auto path = CreateFile("not a library");
    ASSERT_FALSE(path.empty());
    int mockHandle = 0;
    auto mockDecrypt = reinterpret_cast<void *>(MockDecrypt);
    MOCKER(SystemAdapter::DlOpen).expects(once()).will(returnValue(static_cast<void *>(&mockHandle)));
    MOCKER(SystemAdapter::DlSym).expects(once()).will(returnValue(mockDecrypt));
    UbsCryptorHandler handler;
    handler.SetDecryptLibPath(path);

    ASSERT_EQ(handler.Initialize(), 0);
    handler.SetDecryptLibPath("/tmp/ignored_after_initialize.so");
    EXPECT_EQ(handler.Initialize(), 0);
    EXPECT_EQ(UbsCryptorHandler::SetCryptorLogger(TestLogger), 0);
}
} // namespace UT
