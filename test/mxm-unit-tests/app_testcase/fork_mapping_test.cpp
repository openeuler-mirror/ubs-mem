/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#include <gtest/gtest.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include "fork_resources.h"
#include "rack_mem_fd_map.h"

namespace {
void CheckKernelExcludesMapping(void *address, size_t size)
{
    const auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        for (size_t offset = 0; offset < size; offset += page) {
            unsigned char resident = 0;
            errno = 0;
            if (mincore(static_cast<char *>(address) + offset, page, &resident) != -1 || errno != ENOMEM) {
                _exit(1);
            }
        }
        _exit(0);
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    *static_cast<volatile unsigned char *>(address) = 0x2b;
    EXPECT_EQ(*static_cast<volatile unsigned char *>(address), 0x2b);
}
} // namespace

TEST(ForkMappingTest, AnonymousReservationDoesNotRequireResourceAdvice)
{
    void *address = nullptr;
    const auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    ASSERT_EQ(ock::mxmd::RackMemFdMap::MapAnonymousMemory(nullptr, page, address), 0U);
    ASSERT_EQ(mprotect(address, page, PROT_READ | PROT_WRITE), 0);
    // This is an unpublished reservation; only a final resource mapping is marked DONTFORK.
    *static_cast<volatile unsigned char *>(address) = 0x2b;
    EXPECT_EQ(munmap(address, page), 0);
}

TEST(ForkMappingTest, FixedReplacementReappliesDontForkAndSurvivesClose)
{
    const auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const int fd = static_cast<int>(syscall(SYS_memfd_create, "fork_mapping_test", 0));
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, static_cast<off_t>(page * 2)), 0);
    void *address = nullptr;
    ASSERT_EQ(ock::mxmd::RackMemFdMap::MapAnonymousMemory(nullptr, page * 2, address), 0U);
    ASSERT_EQ(ock::mxmd::RackMemFdMap::GetInstance().MapForEachFd(address, page * 2, fd, PROT_READ | PROT_WRITE,
                                                                  MAP_SHARED | MAP_FIXED),
              0U);
    ASSERT_EQ(close(fd), 0);
    CheckKernelExcludesMapping(address, page * 2);
    EXPECT_EQ(munmap(address, page * 2), 0);
}
