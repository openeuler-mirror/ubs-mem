/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-mem is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include "ubs_mem.h"

namespace {
constexpr size_t SHM_SIZE = 128UL * 1024UL * 1024UL;

int RunExample(char *providerNames[], size_t providerCount)
{
    std::vector<std::array<char, MAX_SHM_NAME_LENGTH>> shmNames(providerCount);
    std::vector<void *> mappedAddresses(providerCount, nullptr);
    size_t allocatedCount = 0;
    size_t mappedCount = 0;
    void *reservedAddress = MAP_FAILED;
    int result = 1;

    do {
        bool allocationSucceeded = true;
        for (size_t i = 0; i < providerCount; ++i) {
            ubs_mem_provider_t provider = {};
            int ret = std::snprintf(provider.host_name, sizeof(provider.host_name), "%s", providerNames[i]);
            if (ret < 0 || static_cast<size_t>(ret) >= sizeof(provider.host_name)) {
                std::fprintf(stderr, "Provider hostname is too long: %s\n", providerNames[i]);
                allocationSucceeded = false;
                break;
            }
            provider.socket_id = std::numeric_limits<uint32_t>::max();
            provider.numa_id = std::numeric_limits<uint32_t>::max();
            provider.port_id = std::numeric_limits<uint32_t>::max();

            ret = std::snprintf(shmNames[i].data(), shmNames[i].size(), "ubsm_fixed_%ld_%zu",
                                static_cast<long>(getpid()), i);
            if (ret < 0 || static_cast<size_t>(ret) >= shmNames[i].size()) {
                std::fprintf(stderr, "Failed to construct shared-memory name %zu.\n", i);
                allocationSucceeded = false;
                break;
            }

            ret = ubsmem_shmem_allocate_with_provider(&provider, shmNames[i].data(), SHM_SIZE, S_IRUSR | S_IWUSR,
                                                      UBSM_FLAG_NONCACHE | UBSM_FLAG_WR_DELAY_COMP);
            if (ret != UBSM_OK) {
                std::fprintf(stderr, "Failed to allocate %s from provider %s: %d\n", shmNames[i].data(),
                             provider.host_name, ret);
                allocationSucceeded = false;
                break;
            }
            ++allocatedCount;
            std::printf("Allocated %s from provider %s\n", shmNames[i].data(), provider.host_name);
        }
        if (!allocationSucceeded) {
            break;
        }

        // MAP_FIXED may replace existing mappings, so reserve a known-safe range before using it.
        reservedAddress = mmap(nullptr, providerCount * SHM_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reservedAddress == MAP_FAILED) {
            std::perror("Failed to reserve the contiguous virtual address range");
            break;
        }

        bool mapSucceeded = true;
        for (size_t i = 0; i < providerCount; ++i) {
            void *targetAddress = static_cast<char *>(reservedAddress) + i * SHM_SIZE;
            void *actualAddress = nullptr;
            int ret = ubsmem_shmem_map(targetAddress, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
                                       shmNames[i].data(), 0, &actualAddress);
            if (ret != UBSM_OK) {
                std::fprintf(stderr, "Failed to map %s at %p: %d\n", shmNames[i].data(), targetAddress, ret);
                mapSucceeded = false;
                break;
            }
            mappedAddresses[i] = actualAddress;
            ++mappedCount;
        }
        if (!mapSucceeded) {
            break;
        }

        for (size_t i = 0; i < providerCount; ++i) {
            void *segment = static_cast<char *>(reservedAddress) + i * SHM_SIZE;
            std::memset(segment, static_cast<unsigned char>(i + 1), SHM_SIZE);
        }

        bool accessSucceeded = true;
        for (size_t i = 0; i < providerCount; ++i) {
            const auto *segment = reinterpret_cast<const unsigned char *>(reservedAddress) + i * SHM_SIZE;
            const auto expectedValue = static_cast<unsigned char>(i + 1);
            for (size_t offset = 0; offset < SHM_SIZE; ++offset) {
                if (segment[offset] != expectedValue) {
                    std::fprintf(stderr, "Failed to verify segment %zu at offset %zu: expected 0x%02x, got 0x%02x.\n",
                                 i, offset, static_cast<unsigned int>(expectedValue),
                                 static_cast<unsigned int>(segment[offset]));
                    accessSucceeded = false;
                    break;
                }
            }
            if (!accessSucceeded) {
                break;
            }
            std::printf("segment[%zu] %p-%p: pattern=0x%02x\n", i, static_cast<const void *>(segment),
                        static_cast<const void *>(segment + SHM_SIZE), static_cast<unsigned int>(expectedValue));
        }
        if (!accessSucceeded) {
            break;
        }
        result = 0;
    } while (false);

    // Unmap the shared-memory objects that were mapped successfully.
    for (size_t i = mappedCount; i > 0; --i) {
        const size_t index = i - 1;
        int ret = ubsmem_shmem_unmap(mappedAddresses[index], SHM_SIZE);
        if (ret != UBSM_OK) {
            std::fprintf(stderr, "Failed to unmap %s: %d\n", shmNames[index].data(), ret);
            result = 1;
        }
    }

    // Release the part of the anonymous reservation that was not replaced by shared memory.
    if (reservedAddress != MAP_FAILED && mappedCount < providerCount) {
        void *remainingAddress = static_cast<char *>(reservedAddress) + mappedCount * SHM_SIZE;
        size_t remainingSize = (providerCount - mappedCount) * SHM_SIZE;
        if (munmap(remainingAddress, remainingSize) != 0) {
            std::perror("Failed to release the remaining reserved address range");
            result = 1;
        }
    }

    // Delete the shared-memory objects that were allocated successfully.
    for (size_t i = allocatedCount; i > 0; --i) {
        const size_t index = i - 1;
        int ret = ubsmem_shmem_deallocate(shmNames[index].data());
        if (ret != UBSM_OK) {
            std::fprintf(stderr, "Failed to deallocate %s: %d\n", shmNames[index].data(), ret);
            result = 1;
        }
    }
    return result;
}
} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <host_num> <host_0> ... <host_n>\n", argv[0]);
        return 1;
    }

    char *end = nullptr;
    errno = 0;
    unsigned long hostCount = std::strtoul(argv[1], &end, 10);
    if (argv[1][0] < '0' || argv[1][0] > '9' || errno != 0 || end == argv[1] || *end != '\0' || hostCount == 0 ||
        hostCount > MAX_HOST_NUM || argc != static_cast<int>(hostCount + 2)) {
        std::fprintf(stderr, "host_num must be between 1 and %d and match the number of hostnames.\n", MAX_HOST_NUM);
        return 1;
    }

    ubsmem_options_t options = {};
    int ret = ubsmem_init_attributes(&options);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_init_attributes failed: %d\n", ret);
        return 1;
    }
    ret = ubsmem_initialize(&options);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_initialize failed: %d\n", ret);
        return 1;
    }

    int result = RunExample(&argv[2], static_cast<size_t>(hostCount));
    ret = ubsmem_finalize();
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_finalize failed: %d\n", ret);
        result = 1;
    }
    return result;
}
