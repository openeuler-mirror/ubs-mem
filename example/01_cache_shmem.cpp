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

#include <cstdio>
#include <cstring>

#include "ubs_mem.h"

int main()
{
    constexpr size_t SHM_SIZE = 128UL * 1024UL * 1024UL;
    constexpr char MESSAGE[] = "Hello from UBS Memory cache mode";
    char shmName[MAX_SHM_NAME_LENGTH + 1] = {};
    ubsmem_options_t options = {};
    void *address = nullptr;
    bool initialized = false;
    bool allocated = false;
    bool mapped = false;
    int result = 1;
    int ret = std::snprintf(shmName, sizeof(shmName), "ubsm_cache_example_%ld", static_cast<long>(getpid()));
    if (ret < 0 || static_cast<size_t>(ret) >= sizeof(shmName)) {
        std::fprintf(stderr, "Failed to construct the shared-memory name.\n");
        return result;
    }

    ret = ubsmem_init_attributes(&options);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_init_attributes failed: %d\n", ret);
        return result;
    }
    ret = ubsmem_initialize(&options);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_initialize failed: %d\n", ret);
        return result;
    }
    initialized = true;

    ret = ubsmem_shmem_allocate("default", shmName, SHM_SIZE, S_IRUSR | S_IWUSR, UBSM_FLAG_CACHE);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_shmem_allocate failed: %d\n", ret);
        goto cleanup;
    }
    allocated = true;

    ret = ubsmem_shmem_map(nullptr, SHM_SIZE, PROT_NONE, MAP_SHARED, shmName, 0, &address);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "ubsmem_shmem_map failed: %d\n", ret);
        goto cleanup;
    }
    mapped = true;

    ret = ubsmem_shmem_set_ownership(shmName, address, SHM_SIZE, PROT_READ | PROT_WRITE);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Failed to acquire write ownership: %d\n", ret);
        goto cleanup;
    }
    std::memcpy(address, MESSAGE, sizeof(MESSAGE));

    // Releasing ownership flushes and invalidates the cache for this mapping.
    ret = ubsmem_shmem_set_ownership(shmName, address, SHM_SIZE, PROT_NONE);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Failed to flush the shared-memory cache: %d\n", ret);
        goto cleanup;
    }
    ret = ubsmem_shmem_set_ownership(shmName, address, SHM_SIZE, PROT_READ);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Failed to acquire read ownership: %d\n", ret);
        goto cleanup;
    }
    if (std::memcmp(address, MESSAGE, sizeof(MESSAGE)) != 0) {
        std::fprintf(stderr, "Shared-memory data verification failed.\n");
        goto cleanup;
    }
    std::printf("Read from %s: %s\n", shmName, static_cast<const char *>(address));

    ret = ubsmem_shmem_set_ownership(shmName, address, SHM_SIZE, PROT_NONE);
    if (ret != UBSM_OK) {
        std::fprintf(stderr, "Failed to release read ownership: %d\n", ret);
        goto cleanup;
    }
    result = 0;

cleanup:
    if (mapped) {
        ret = ubsmem_shmem_unmap(address, SHM_SIZE);
        if (ret != UBSM_OK) {
            std::fprintf(stderr, "ubsmem_shmem_unmap failed: %d\n", ret);
            result = 1;
        }
    }
    if (allocated) {
        ret = ubsmem_shmem_deallocate(shmName);
        if (ret != UBSM_OK) {
            std::fprintf(stderr, "ubsmem_shmem_deallocate failed: %d\n", ret);
            result = 1;
        }
    }
    if (initialized) {
        ret = ubsmem_finalize();
        if (ret != UBSM_OK) {
            std::fprintf(stderr, "ubsmem_finalize failed: %d\n", ret);
            result = 1;
        }
    }
    return result;
}
