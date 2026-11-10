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

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "ubs_mem.h"

namespace {
constexpr size_t PROVIDER_SIZE = 128UL * 1024 * 1024;
constexpr size_t INTERLEAVE_SIZE = 4UL * 1024 * 1024;
constexpr size_t MEMBER_SUFFIX_LENGTH = 4;

bool ParseProviders(int argc, char *argv[], ubs_mem_providers_t &providers)
{
    if (argc < 4 || argv[1][0] < '0' || argv[1][0] > '9') {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const auto count = std::strtoul(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || count < 2 || count > MAX_MULTINODE_HOST_NUM ||
        static_cast<size_t>(argc - 2) != count || count > std::numeric_limits<size_t>::max() / PROVIDER_SIZE) {
        return false;
    }
    providers.host_count = static_cast<uint32_t>(count);
    providers.socket_id = 0;
    for (size_t index = 0; index < count; ++index) {
        const auto *host = argv[index + 2];
        const auto length = std::strlen(host);
        if (length == 0 || length >= sizeof(providers.host_names[index])) {
            std::fprintf(stderr, "Invalid provider hostname: index=%zu, length=%zu\n", index, length);
            return false;
        }
        for (size_t previous = 0; previous < index; ++previous) {
            if (std::strcmp(providers.host_names[previous], host) == 0) {
                std::fprintf(stderr, "Duplicate provider hostname: %s\n", host);
                return false;
            }
        }
        std::memcpy(providers.host_names[index], host, length + 1);
    }
    return true;
}

bool ShowInterleavedMapping(void *address, size_t size, const ubs_mem_providers_t &providers)
{
    auto *bytes = static_cast<unsigned char *>(address);
    for (size_t offset = 0; offset < size; offset += INTERLEAVE_SIZE) {
        const auto chunk = offset / INTERLEAVE_SIZE;
        const auto provider = chunk % providers.host_count;
        const uint64_t expected = static_cast<uint64_t>(chunk) + 1;
        // Ownership is acquired by the caller. Volatile keeps the actual shared-memory write and read.
        auto *marker = reinterpret_cast<volatile uint64_t *>(bytes + offset);
        *marker = expected;
        const uint64_t actual = *marker;
        if (actual != expected) {
            std::fprintf(stderr, "Data mismatch: chunk=%zu, provider=%zu, offset=%zu\n", chunk, provider, offset);
            return false;
        }
        std::printf("chunk[%zu] host=%s, providerOffset=%zu, virtual=%p-%p, marker=%llu\n", chunk,
                    providers.host_names[provider], (chunk / providers.host_count) * INTERLEAVE_SIZE,
                    static_cast<void *>(bytes + offset), static_cast<void *>(bytes + offset + INTERLEAVE_SIZE),
                    static_cast<unsigned long long>(actual));
    }
    return true;
}

bool AccessInterleavedMapping(const char *name, void *address, size_t size, const ubs_mem_providers_t &providers)
{
    auto ret = ubsmem_shmem_set_ownership(name, address, size, PROT_READ | PROT_WRITE);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Acquire ownership failed: name=%s, ret=%d\n", name, ret);
        return false;
    }
    const auto verified = ShowInterleavedMapping(address, size, providers);
    // Release ownership even when verification fails, flushing and invalidating the cache.
    ret = ubsmem_shmem_set_ownership(name, address, size, PROT_NONE);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Release ownership failed: name=%s, ret=%d\n", name, ret);
        return false;
    }
    return verified;
}

int CleanupPool(const char *name, void *address, size_t size, bool verified)
{
    if (address != nullptr) {
        // Interleaved mappings use the same unmap API as ordinary mappings.
        const auto ret = ubsmem_shmem_unmap(address, size);
        if (ret != UBSM_OK) {
            std::fprintf(stderr, "Unmap failed: name=%s, address=%p, size=%zu, ret=%d; skipping deletion.\n", name,
                         address, size, ret);
            return 1;
        }
    }
    const auto ret = ubsmem_shmem_deallocate_with_multi_provider(name);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Delete pool failed: name=%s, ret=%d\n", name, ret);
        return 1;
    }
    std::printf("Deleted pool %s\n", name);
    return verified ? 0 : 1;
}

int RunExample(const ubs_mem_providers_t &providers)
{
    char name[MAX_SHM_NAME_LENGTH]{};
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    const auto length = std::snprintf(name, sizeof(name), "il_%lx_%llx", static_cast<unsigned long>(getpid()),
                                      static_cast<unsigned long long>(timestamp));
    if (length < 0 || static_cast<size_t>(length) + MEMBER_SUFFIX_LENGTH >= sizeof(name)) {
        std::fprintf(stderr, "Failed to construct the pool name.\n");
        return 1;
    }
    const auto size = static_cast<size_t>(providers.host_count) * PROVIDER_SIZE;
    // No CUSTOM_SIZE flag: the SDK divides size equally in host_names order.
    auto ret = ubsmem_shmem_allocate_with_multi_provider(name, size, S_IRUSR | S_IWUSR, UBSM_FLAG_CACHE, &providers);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Allocate pool failed: name=%s, size=%zu, ret=%d\n", name, size, ret);
        return 1;
    }
    std::printf("Created pool %s: providers=%u, socket=%u, flags=CC, bytesPerProvider=%zu, totalBytes=%zu, "
                "interleaveBytes=%zu\n",
                name, providers.host_count, providers.socket_id, PROVIDER_SIZE, size, INTERLEAVE_SIZE);
    void *address = nullptr;
    ret = ubsmem_shmem_map_interleaved(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, name, INTERLEAVE_SIZE,
                                       &address);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Interleaved map failed: name=%s, ret=%d\n", name, ret);
        return CleanupPool(name, address, size, false);
    }
    std::printf("Interleaved mapping succeeded: address=%p, size=%zu\n", address, size);
    const auto verified = AccessInterleavedMapping(name, address, size, providers);
    return CleanupPool(name, address, size, verified);
}
} // namespace

int main(int argc, char *argv[])
{
    ubs_mem_providers_t providers{};
    if (!ParseProviders(argc, argv, providers)) {
        std::fprintf(stderr,
                     "Usage: %s <host_num> <host_0> ... <host_n>\n"
                     "Provide 2..%u distinct hosts; each host contributes 128 MiB.\n",
                     argv[0], static_cast<unsigned int>(MAX_MULTINODE_HOST_NUM));
        return 1;
    }
    ubsmem_options_t options{};
    auto ret = ubsmem_init_attributes(&options);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_init_attributes failed: %d\n", ret);
        return 1;
    }
    ret = ubsmem_initialize(&options);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_initialize failed: %d\n", ret);
        return 1;
    }
    const auto result = RunExample(providers);
    ret = ubsmem_finalize();
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_finalize failed: %d\n", ret);
        return 1;
    }
    return result;
}
