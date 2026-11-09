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
constexpr size_t MIN_PROVIDER_COUNT = 2;
constexpr size_t MEMBER_SUFFIX_LENGTH = 4;
constexpr size_t MIB = 1024UL * 1024;

struct ExampleConfig {
    ubs_mem_providers_t providers{};
    size_t totalSize{};
    uint64_t flags{UBSM_FLAG_NONCACHE};
    int logLevel{1};
};

void PrintUsage(const char *program)
{
    std::fprintf(stderr,
                 "Usage: %s [--log-level 0..4] [--flags <decimal|0xHEX>] <host_num> <host_0> <size_0_MiB> ...\n"
                 "       %s [--log-level 0..4] [--flags <decimal|0xHEX>] <host_num> <host_0> ... <host_n>\n"
                 "Sizes must be positive multiples of 4 MiB. Host-only mode defaults to 128 MiB per host.\n"
                 "Log levels: 0=debug, 1=info, 2=warning, 3=error, 4=critical; default=1.\n"
                 "Default flags=0x2 (NONCACHE). Explicit sizes automatically enable CUSTOM_SIZE (0x80).\n",
                 program, program);
}

bool ParseUnsigned(const char *text, int base, uint64_t &value)
{
    if (text[0] < '0' || text[0] > '9') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    const auto parsed = std::strtoull(text, &end, base);
    if (errno != 0 || end == text || *end != '\0' || parsed > std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    value = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseOptions(int argc, char *argv[], ExampleConfig &config, int &position)
{
    while (position < argc && std::strncmp(argv[position], "--", 2) == 0) {
        const char *option = argv[position];
        uint64_t value{};
        if (position + 1 >= argc ||
            !ParseUnsigned(argv[position + 1], std::strcmp(option, "--flags") == 0 ? 0 : 10, value)) {
            std::fprintf(stderr, "Missing or invalid value for %s.\n", option);
            return false;
        }
        if (std::strcmp(option, "--log-level") == 0 && value <= 4) {
            config.logLevel = static_cast<int>(value);
        } else if (std::strcmp(option, "--flags") == 0) {
            config.flags = value;
        } else {
            std::fprintf(stderr, "Unknown option or invalid log level: %s.\n", option);
            return false;
        }
        position += 2;
    }
    // This example remaps the pool after its last reference is released; anonymous reclamation would delete it.
    if ((config.flags & (UBSM_FLAG_WITH_LOCK | UBSM_FLAG_MEM_ANONYMOUS)) != 0) {
        std::fprintf(stderr, "WITH_LOCK and MEM_ANONYMOUS are unsupported by this two-layout example.\n");
        return false;
    }
    return true;
}

bool SetProvider(const char *name, const char *sizeText, size_t index, ExampleConfig &config)
{
    auto &providers = config.providers;
    const auto length = std::strlen(name);
    uint64_t sizeMiB = PROVIDER_SIZE / MIB;
    if (length == 0 || length >= sizeof(providers.host_names[index]) ||
        (sizeText != nullptr && !ParseUnsigned(sizeText, 10, sizeMiB)) || sizeMiB == 0 || sizeMiB % 4 != 0 ||
        sizeMiB > std::numeric_limits<size_t>::max() / MIB) {
        std::fprintf(stderr, "Invalid provider: index=%zu, host=%s, sizeMiB=%s.\n", index, name,
                     sizeText == nullptr ? "128" : sizeText);
        return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
        if (std::strcmp(providers.host_names[previous], name) == 0) {
            std::fprintf(stderr, "Duplicate provider hostname: %s\n", name);
            return false;
        }
    }
    const auto size = static_cast<size_t>(sizeMiB) * MIB;
    if (size > std::numeric_limits<size_t>::max() - config.totalSize) {
        std::fprintf(stderr, "Total pool size overflow: index=%zu, host=%s, size=%zu.\n", index, name, size);
        return false;
    }
    std::memcpy(providers.host_names[index], name, length + 1);
    providers.host_sizes[index] = size;
    config.totalSize += size;
    std::printf("provider[%zu]=%s, size=%zu MiB (%zu bytes)\n", index, name, static_cast<size_t>(sizeMiB), size);
    return true;
}

bool ParseProviders(int argc, char *argv[], ExampleConfig &config)
{
    int position = 1;
    uint64_t count{};
    if (!ParseOptions(argc, argv, config, position) || position >= argc || !ParseUnsigned(argv[position], 10, count) ||
        count < MIN_PROVIDER_COUNT || count > MAX_MULTINODE_HOST_NUM) {
        return false;
    }
    ++position;
    const bool customSizes = argc - position == static_cast<int>(count * 2);
    if (!customSizes && argc - position != static_cast<int>(count)) {
        std::fprintf(stderr, "Expected %llu hostnames or %llu host/size pairs.\n",
                     static_cast<unsigned long long>(count), static_cast<unsigned long long>(count));
        return false;
    }
    auto &providers = config.providers;
    providers.host_count = static_cast<uint32_t>(count);
    providers.socket_id = 0;
    if (customSizes) {
        config.flags |= UBSM_FLAG_MULTINODE_CUSTOM_SIZE;
    }
    for (size_t index = 0; index < count; ++index) {
        const char *name = argv[position++];
        const char *sizeText = customSizes ? argv[position++] : nullptr;
        if (!SetProvider(name, sizeText, index, config)) {
            return false;
        }
    }
    std::printf("Pool size=%zu, flags=0x%llx, SDK log level=%d\n", config.totalSize,
                static_cast<unsigned long long>(config.flags), config.logLevel);
    return true;
}

bool CheckChunk(unsigned char *bytes, size_t offset, size_t size, size_t provider, bool initialize)
{
    const auto pattern = static_cast<unsigned char>(provider + 1);
    if (initialize) {
        std::memset(bytes + offset, pattern, size);
    }
    for (size_t index = 0; index < size; ++index) {
        if (bytes[offset + index] != pattern) {
            std::fprintf(stderr, "Data mismatch: provider=%zu, offset=%zu, expected=0x%02x, actual=0x%02x\n", provider,
                         offset + index, static_cast<unsigned int>(pattern),
                         static_cast<unsigned int>(bytes[offset + index]));
            return false;
        }
    }
    std::printf("%s offset=%zu, provider=%zu, address=%p-%p, pattern=0x%02x\n",
                initialize ? "consecutive" : "interleaved", offset, provider, static_cast<void *>(bytes + offset),
                static_cast<void *>(bytes + offset + size), static_cast<unsigned int>(pattern));
    return true;
}

bool CheckLayout(void *address, const ExampleConfig &config, bool interleaved)
{
    const auto &providers = config.providers;
    size_t consumed[MAX_MULTINODE_HOST_NUM] = {};
    auto *bytes = static_cast<unsigned char *>(address);
    size_t offset{};
    while (offset < config.totalSize) {
        for (size_t provider = 0; provider < providers.host_count; ++provider) {
            if (consumed[provider] == providers.host_sizes[provider]) {
                continue;
            }
            const auto size = interleaved ? INTERLEAVE_SIZE : providers.host_sizes[provider];
            if (!CheckChunk(bytes, offset, size, provider, !interleaved)) {
                return false;
            }
            consumed[provider] += size;
            offset += size;
        }
    }
    return true;
}

bool UnmapPool(void *&address, size_t size)
{
    if (address == nullptr) {
        return true;
    }
    // Both layouts use the same unmap API. A failed cleanup can be retried with the original address and size.
    for (size_t attempt = 0; attempt < 2; ++attempt) {
        const auto ret = ubsmem_shmem_unmap(address, size);
        if (ret == UBSM_OK) {
            address = nullptr;
            return true;
        }
        std::fprintf(stderr, "Failed to unmap pool: address=%p, size=%zu, attempt=%zu, ret=%d\n", address, size,
                     attempt + 1, ret);
    }
    return false;
}

bool WaitForContinue(const char *stage)
{
    std::printf("%s succeeded. Enter c to continue: ", stage);
    std::fflush(stdout);
    char input[64] = {};
    while (std::fgets(input, sizeof(input), stdin) != nullptr) {
        if (std::strcmp(input, "c\n") == 0 || std::strcmp(input, "c\r\n") == 0 || std::strcmp(input, "c") == 0) {
            return true;
        }
        std::printf("Enter c to continue: ");
        std::fflush(stdout);
    }
    std::fprintf(stderr, "Input closed while waiting after %s; cleaning up.\n", stage);
    return false;
}

bool SetOwnership(const char *name, void *address, const ExampleConfig &config, int prot)
{
    if ((config.flags & (UBSM_FLAG_NONCACHE | UBSM_FLAG_ONLY_IMPORT_NONCACHE)) != 0) {
        return true;
    }
    const auto ret = ubsmem_shmem_set_ownership(name, address, config.totalSize, prot);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Ownership switch failed: name=%s, prot=%d, ret=%d\n", name, prot, ret);
        return false;
    }
    return true;
}

bool RunMappings(const char *name, const ExampleConfig &config, void *&address)
{
    const auto size = config.totalSize;
    auto ret =
        ubsmem_shmem_map_with_multi_provider(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, name, 0, &address);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Consecutive pool mapping failed: name=%s, ret=%d\n", name, ret);
        return false;
    }
    if (!WaitForContinue("Consecutive pool mapping") || !SetOwnership(name, address, config, PROT_READ | PROT_WRITE) ||
        !CheckLayout(address, config, false) || !SetOwnership(name, address, config, PROT_NONE) ||
        !UnmapPool(address, size)) {
        return false;
    }
    // Remap the same backing memory: each chunk must retain the corresponding provider's pattern.
    ret = ubsmem_shmem_map_interleaved(nullptr, size, PROT_READ, MAP_SHARED, name, INTERLEAVE_SIZE, &address);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Interleaved pool mapping failed: name=%s, chunkSize=%zu, ret=%d\n", name, INTERLEAVE_SIZE,
                     ret);
        return false;
    }
    return WaitForContinue("Interleaved pool mapping") && SetOwnership(name, address, config, PROT_READ) &&
           CheckLayout(address, config, true) && SetOwnership(name, address, config, PROT_NONE);
}

int RunExample(const ExampleConfig &config)
{
    char name[MAX_SHM_NAME_LENGTH] = {};
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    const auto length = std::snprintf(name, sizeof(name), "ubsm_multi_%ld_%lld", static_cast<long>(getpid()),
                                      static_cast<long long>(timestamp));
    if (length < 0 || static_cast<size_t>(length) + MEMBER_SUFFIX_LENGTH >= sizeof(name)) {
        std::fprintf(stderr, "Failed to construct the pool name.\n");
        return 1;
    }
    const auto size = config.totalSize;
    auto ret =
        ubsmem_shmem_allocate_with_multi_provider(name, size, S_IRUSR | S_IWUSR, config.flags, &config.providers);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Failed to allocate pool: name=%s, size=%zu, ret=%d\n", name, size, ret);
        return 1;
    }
    std::printf("Allocated pool %s, size=%zu\n", name, size);
    void *address = nullptr;
    const bool verified = WaitForContinue("Pool creation") && RunMappings(name, config, address);
    if (!UnmapPool(address, size)) {
        std::fprintf(stderr, "Pool %s still has a mapping; skipping deletion.\n", name);
        return 1;
    }
    ret = ubsmem_shmem_deallocate_with_multi_provider(name);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Failed to deallocate pool: name=%s, ret=%d\n", name, ret);
        return 1;
    }
    return verified ? 0 : 1;
}
} // namespace

int main(int argc, char *argv[])
{
    ExampleConfig config;
    if (!ParseProviders(argc, argv, config)) {
        PrintUsage(argv[0]);
        return 1;
    }
    auto ret = ubsmem_set_logger_level(config.logLevel);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_set_logger_level failed: level=%d, ret=%d\n", config.logLevel, ret);
        return 1;
    }
    ubsmem_options_t options = {};
    ret = ubsmem_init_attributes(&options);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_init_attributes failed: %d\n", ret);
        return 1;
    }
    ret = ubsmem_initialize(&options);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_initialize failed: %d\n", ret);
        return 1;
    }
    const auto result = RunExample(config);
    ret = ubsmem_finalize();
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_finalize failed: %d\n", ret);
        return 1;
    }
    return result;
}
