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
#ifndef __UBSM_SHMEM_H__
#define __UBSM_SHMEM_H__

#include <sys/types.h>
#include <ubs_mem_def.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize an options structure for ubsmem_initialize().
 *
 * The current API has no configurable options, but callers should use this function so that newly added options can
 * be initialized by future versions of the library.
 *
 * @param ubsm_shmem_opts [out] Options structure to initialize. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_init_attributes(ubsmem_options_t *ubsm_shmem_opts);

/**
 * @brief Initialize the ubs-mem library for the current process.
 *
 * Call this function before using shared-memory or leased-memory APIs.
 *
 * @param ubsm_shmem_opts [in] Options initialized by ubsmem_init_attributes(). Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_initialize(const ubsmem_options_t *ubsm_shmem_opts);

/**
 * @brief Finalize the ubs-mem library for the current process.
 *
 * Releases process-local library resources and disconnects the IPC client. Do not call other ubs-mem APIs after
 * finalization unless the library is initialized again.
 *
 * @return UBSM_OK.
 */
SHMEM_API int ubsmem_finalize(void);

/**
 * @brief Set the library log level.
 *
 * @param level [in] Log level: debug (0), info (1), warning (2), error (3), or critical (4).
 * @return UBSM_OK on success; otherwise, UBSM_ERR_PARAM_INVALID.
 */
SHMEM_API int ubsmem_set_logger_level(int level);

/**
 * @brief Register an external log callback.
 *
 * The callback can route ubs-mem messages to the application's logging system. Without an external callback, log
 * messages are written to stdout.
 *
 * @param func [in] Non-NULL callback that receives the log level and a null-terminated message.
 * @return UBSM_OK on success; otherwise, UBSM_ERR_PARAM_INVALID.
 */
SHMEM_API int ubsmem_set_extern_logger(void (*func)(int level, const char *msg));

/**
 * @brief Query the resource region consisting of the local node and all directly connected nodes.
 *
 * @param regions [out] Region topology consisting of the local node and all directly connected nodes. Must not be
 * NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_lookup_regions(ubsmem_regions_t *regions);

/**
 * @brief Create a resource region on the local node.
 *
 * A region selects the provider nodes from which shared or leased memory can be allocated. The reserved name
 * "default" cannot be created by the application.
 *
 * @param region_name [in] Non-empty region name of at most MAX_REGION_NAME_DESC_LENGTH - 1 characters.
 * @param size [in] Reserved for region quota; must be 0.
 * @param reg_attr [in] Region nodes and affinity attributes. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_create_region(const char *region_name, size_t size, const ubsmem_region_attributes_t *reg_attr);

/**
 * @brief Query a resource region by name on the local node.
 *
 * @param region_name [in] Non-empty region name of at most MAX_REGION_NAME_DESC_LENGTH - 1 characters.
 * @param region_desc [out] Region information. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_lookup_region(const char *region_name, ubsmem_region_desc_t *region_desc);

/**
 * @brief Destroy a resource region on the local node.
 *
 * Existing shared-memory objects and leased memory created through the region remain usable. The reserved
 * "default" region cannot be destroyed.
 *
 * @param region_name [in] Non-empty region name of at most MAX_REGION_NAME_DESC_LENGTH - 1 characters.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_destroy_region(const char *region_name);

/**
 * @brief Create a named shared-memory object in a resource region.
 *
 * @note The flags argument accepts UBSM_FLAG_* creation flags, not the MAP_* flags accepted by
 * ubsmem_shmem_map(). UBSM_FLAG_CACHE is 0 and therefore selects cache mode when no other access-mode flag is set.
 * UBSM_FLAG_WR_DELAY_COMP is valid only with UBSM_FLAG_NONCACHE or UBSM_FLAG_ONLY_IMPORT_NONCACHE.
 * UBSM_FLAG_MEM_ANONYMOUS and UBSM_FLAG_MMAP_HUGETLB_PMD are optional modifiers. Access-mode flags such as
 * UBSM_FLAG_WITH_LOCK, UBSM_FLAG_NONCACHE, and UBSM_FLAG_ONLY_IMPORT_NONCACHE are mutually exclusive.
 * To use the lock APIs, set flags to UBSM_FLAG_WITH_LOCK without additional modifiers.
 *
 * @param region_name [in] Resource region name, or "default" for the default region.
 * @param name [in] Globally unique, non-empty object name of at most MAX_SHM_NAME_LENGTH bytes.
 * @param size [in] Requested size in bytes. It must be positive and aligned to 4 MB.
 * @param mode [in] Unix read/write permission bits used to control access to the object.
 * @param flags [in] A valid combination of UBSM_FLAG_* shared-memory creation flags from ubs_mem_def.h.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_allocate(const char *region_name, const char *name, size_t size, mode_t mode,
                                    uint64_t flags);

/**
 * @brief Create a named shared-memory object from a specified provider.
 *
 * @note The flags argument has the same UBSM_FLAG_* rules as ubsmem_shmem_allocate() and must not contain MAP_*
 * mapping flags. Set an unspecified socket_id, numa_id, or port_id field explicitly to UINT32_MAX.
 *
 * @param src_loc [in] Provider location. host_name is required and the other fields select optional topology IDs.
 * @param name [in] Globally unique, non-empty object name of at most MAX_SHM_NAME_LENGTH bytes.
 * @param size [in] Requested size in bytes. It must be positive and aligned to 4 MB.
 * @param mode [in] Unix read/write permission bits used to control access to the object.
 * @param flags [in] A valid combination of UBSM_FLAG_* shared-memory creation flags from ubs_mem_def.h.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_allocate_with_provider(const ubs_mem_provider_t *src_loc, const char *name, size_t size,
                                                  mode_t mode, uint64_t flags);

/**
 * @brief Create a shared-memory pool backed by multiple provider nodes.
 *
 * By default, size is divided equally among all providers. When UBSM_FLAG_MULTINODE_CUSTOM_SIZE is set, each
 * provider uses the corresponding host_sizes entry instead. In that mode, every active host_sizes entry must be
 * positive and 4 MiB aligned, and their sum must equal size.
 *
 * The pool members are named by appending four hexadecimal digits to name. The first two digits encode
 * host_count - 1 and the last two encode the zero-based provider index. For example, a 16-provider pool named
 * "name" contains member "name" at index 0 and members "name0f01" through "name0f0f" at indices 1 through 15.
 *
 * @param name [in] Globally unique, non-empty pool name. It must leave room for the four-character member suffix.
 * @param size [in] Total pool size in bytes. Without custom sizes, it must be evenly divisible by host_count and
 * each provider's share must be 4 MiB aligned.
 * @param mode [in] Unix read/write permission bits used to control access to every pool member.
 * @param flags [in] A valid combination of UBSM_FLAG_* shared-memory creation flags, excluding UBSM_FLAG_WITH_LOCK. Set
 * UBSM_FLAG_MULTINODE_CUSTOM_SIZE to use providers->host_sizes.
 * @param providers [in] Provider list. host_count must be in [2, MAX_MULTINODE_HOST_NUM], and every active
 * host_names entry must be a non-empty, null-terminated host name.
 * providers->socket_id must explicitly select a socket plane on every provider; UINT32_MAX is not allowed.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_allocate_with_multi_provider(const char *name, size_t size, mode_t mode, uint64_t flags,
                                                        const ubs_mem_providers_t *providers);

/**
 * @brief Delete every member of a named multi-node shared-memory pool.
 *
 * Deletion fails if any member is still in use. The caller supplies the original pool name, not a generated member
 * name.
 *
 * @param name [in] Non-empty pool name passed to ubsmem_shmem_allocate_with_multi_provider().
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_deallocate_with_multi_provider(const char *name);

/**
 * @brief Map a multi-node shared-memory pool into the current process without interleaving its members.
 *
 * Only whole-pool mappings are supported. Members are placed consecutively in the host_names order used at creation,
 * using each host's actual allocation size. Release the mapping with ubsmem_shmem_unmap().
 *
 * @note flags accepts mmap-style MAP_* flags, not UBSM_FLAG_* creation flags.
 *
 * @param addr [in] Requested mapping address, or NULL to let the system choose one.
 * @param length [in] Total size of the pool in bytes. Partial mappings are not supported.
 * @param prot [in] Protection: PROT_NONE, PROT_READ, PROT_WRITE, or PROT_READ | PROT_WRITE.
 * @param flags [in] mmap-style MAP_* mapping flags.
 * @param name [in] Name of an existing multi-node shared-memory pool.
 * @param offset [in] Mapping offset. Partial mappings are not supported, so this must be 0.
 * @param local_ptr [out] Mapping base address. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_map_with_multi_provider(void *addr, size_t length, int prot, int flags, const char *name,
                                                   off_t offset, void **local_ptr);

/**
 * @brief Map a multi-node shared-memory pool with provider memory interleaved in fixed-size chunks.
 *
 * Only whole-pool mappings are supported. interleave_size must evenly divide each provider's backing size. For an
 * evenly divided pool, this is equivalent to (length / host_count) % interleave_size == 0.
 * Chunks are placed in creation-time host_names order; exhausted providers are skipped when sizes differ.
 * Chunks may span multiple backing memory IDs. interleave_size must be page aligned (huge-page aligned when enabled).
 * Release the mapping with ubsmem_shmem_unmap().
 *
 * @note flags accepts mmap-style MAP_* flags, not UBSM_FLAG_* creation flags.
 *
 * @param addr [in] Requested mapping address, or NULL to let the system choose one.
 * @param length [in] Total size of the pool in bytes. Partial mappings are not supported.
 * @param prot [in] Protection: PROT_NONE, PROT_READ, PROT_WRITE, or PROT_READ | PROT_WRITE.
 * @param flags [in] mmap-style MAP_* mapping flags.
 * @param name [in] Name of an existing multi-node shared-memory pool.
 * @param interleave_size [in] Positive interleave chunk size in bytes; it must evenly divide every provider size.
 * @param local_ptr [out] Mapping base address. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_map_interleaved(void *addr, size_t length, int prot, int flags, const char *name,
                                           size_t interleave_size, void **local_ptr);

/**
 * @brief Delete a named shared-memory object.
 *
 * Deletion fails while the object is still in use.
 *
 * @param name [in] Non-empty shared-memory object name of at most MAX_SHM_NAME_LENGTH bytes.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_deallocate(const char *name);

/**
 * @brief Map an entire named shared-memory object into the current process.
 *
 * Only whole-object mappings are supported. A shared-memory object can be mapped only once in a process.
 *
 * @note The flags argument accepts mmap-style MAP_* flags, not the UBSM_FLAG_* creation flags accepted by
 * ubsmem_shmem_allocate(). Use MAP_SHARED, optionally combined with MAP_FIXED or MAP_FIXED_NOREPLACE.
 *
 * @param addr [in] Requested mapping address, or NULL to let the system choose one. A non-NULL address must be
 * aligned to PAGE_SIZE. For an object created with UBSM_FLAG_MMAP_HUGETLB_PMD, it must instead be aligned to the
 * PMD huge-page size. Fixed mappings require a non-NULL address.
 * @param length [in] Full shared-memory object size in bytes. For an object created with
 * UBSM_FLAG_MMAP_HUGETLB_PMD, the size must be aligned to the PMD huge-page size. Partial mappings are not supported.
 * @param prot [in] Protection: PROT_NONE, PROT_READ, PROT_WRITE, or PROT_READ | PROT_WRITE.
 * @param flags [in] MAP_* mapping flags described above.
 * @param name [in] Name of an existing shared-memory object.
 * @param offset [in] Mapping offset. Partial mappings are not supported, so this must be 0.
 * @param local_ptr [out] Mapping base address. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_map(void *addr, size_t length, int prot, int flags, const char *name, off_t offset,
                               void **local_ptr);

/**
 * @brief Unmap a shared-memory object from the current process.
 *
 * Partial unmapping is not supported. A locked object must be unlocked before it can be unmapped.
 * Supports ordinary, multi-provider consecutive, and interleaved mappings. If cleanup fails after local access is
 * revoked, the mapping must not be accessed; retry with the same base address and length to finish cleanup.
 *
 * @param local_ptr [in] Exact base address returned by a shared-memory mapping API.
 * @param length [in] Full mapped object size in bytes.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_unmap(void *local_ptr, size_t length);

/**
 * @brief Change local access permissions and maintain cache consistency for a mapped shared-memory range.
 *
 * For multi-provider pools, the original creation flags determine whether ownership maintenance is needed.
 * A valid range returns success without invoking ownership maintenance when UBSM_FLAG_NONCACHE or
 * UBSM_FLAG_ONLY_IMPORT_NONCACHE is set. Other mappings are processed by their actual address segments.
 *
 * @param name [in] Name of the mapped shared-memory object.
 * @param start [in] Start address within the mapping, aligned to PAGE_SIZE.
 * @param length [in] Positive range length aligned to PAGE_SIZE.
 * @param prot [in] New protection: PROT_NONE, PROT_READ, or PROT_READ | PROT_WRITE.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_set_ownership(const char *name, void *start, size_t length, int prot);

/**
 * @brief Acquire a write lock on a mapped shared-memory object.
 *
 * The object must have been created with flags set exactly to UBSM_FLAG_WITH_LOCK and mapped by the current process.
 * A successful call grants read/write access to the entire mapping.
 *
 * @param name [in] Name of the shared-memory object.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_write_lock(const char *name);

/**
 * @brief Acquire a read lock on a mapped shared-memory object.
 *
 * The object must have been created with flags set exactly to UBSM_FLAG_WITH_LOCK and mapped by the current process.
 * A successful call grants read access to the entire mapping.
 *
 * @param name [in] Name of the shared-memory object.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_read_lock(const char *name);

/**
 * @brief Release the current process's read or write lock on a shared-memory object.
 *
 * A successful call changes the entire mapping to PROT_NONE before releasing the lock.
 *
 * @param name [in] Name of the shared-memory object.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_unlock(const char *name);

/**
 * @brief Query shared-memory objects whose names begin with a prefix.
 *
 * @param prefix [in] Non-empty name prefix of at most MAX_SHM_NAME_LENGTH bytes.
 * @param shm_list [out] Caller-provided array that receives object names and sizes.
 * @param shm_cnt [in,out] On input, capacity of shm_list; on success, number of entries written. If the capacity is
 * insufficient, it is updated to the required count and UBSM_ERR_PARAM_INVALID is returned.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_list_lookup(const char *prefix, ubsmem_shmem_desc_t *shm_list, uint32_t *shm_cnt);

/**
 * @brief Query detailed information about a named shared-memory object.
 *
 * @param name [in] Non-empty shared-memory object name.
 * @param shm_info [out] Object size and backing memory information. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_lookup(const char *name, ubsmem_shmem_info_t *shm_info);

/**
 * @brief Lease memory from a resource region for use by the current process.
 *
 * @param region_name [in] Resource region name, or "default" for the default region.
 * @param size [in] Positive requested size in bytes, aligned to 4 MB.
 * @param mem_distance [in] Provider distance. Currently only DISTANCE_DIRECT_NODE is supported.
 * @param flags [in] Either 0, UBSM_FLAG_MMAP_HUGETLB_PMD, or UBSM_FLAG_MALLOC_WITH_NUMA. These flags cannot be
 * combined.
 * @param local_ptr [out] Address of the leased memory. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_lease_malloc(const char *region_name, size_t size, ubsmem_distance_t mem_distance, uint64_t flags,
                                  void **local_ptr);

/**
 * @brief Lease memory from a specified provider location for use by the current process.
 *
 * @param src_loc [in] Provider slot, socket, NUMA node, and port. Must not be NULL.
 * @param size [in] Positive requested size in bytes, aligned to 4 MB.
 * @param flags [in] Either 0, UBSM_FLAG_MMAP_HUGETLB_PMD, or UBSM_FLAG_MALLOC_WITH_NUMA. These flags cannot be
 * combined.
 * @param local_ptr [out] Address of the leased memory. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_lease_malloc_with_location(const ubs_mem_location_t *src_loc, size_t size, uint64_t flags,
                                                void **local_ptr);

/**
 * @brief Release leased memory.
 *
 * @param local_ptr [in] Exact address returned by ubsmem_lease_malloc() or ubsmem_lease_malloc_with_location().
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_lease_free(void *local_ptr);

/**
 * @brief Query memory statistics for the nodes in the cluster.
 *
 * @param info [out] Per-host and per-NUMA memory statistics. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_lookup_cluster_statistic(ubsmem_cluster_info_t *info);

/**
 * @brief Subscribe to shared-memory UB fault events.
 *
 * @param registerFunc [in] Non-NULL callback invoked with the affected object name and fault type.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_faults_register(shmem_faults_func registerFunc);

/**
 * @brief Query the local node ID within the supernode domain.
 *
 * @param nid [out] Local node ID. Must not be NULL.
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_local_nid_query(uint32_t *nid);

/**
 * @brief Suspend the IPC client connection to ubsmd.
 *
 * The daemon skips process memory-leak cleanup when the suspended client disconnects. Existing mappings remain
 * accessible, but APIs that require IPC cannot be used until ubsmem_shmem_ipc_resume() succeeds.
 *
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_ipc_suspend(void);

/**
 * @brief Resume the IPC client connection to ubsmd.
 *
 * Restarts the IPC client and re-establishes the daemon connection after ubsmem_shmem_ipc_suspend().
 *
 * @return UBSM_OK on success; otherwise, an error code defined by ubsmshmem_ret_t.
 */
SHMEM_API int ubsmem_shmem_ipc_resume(void);

/**
 * Close API admission, drain in-flight operations, suspend IPC and then stop UBSE before fork().
 * Call only from an ordinary thread after initialization. Success is required before fork().
 * Callback contexts and concurrent prepare return UBSM_ERR_BUSY; qualified drain/backend failures do not.
 * An initialized UBSE client is stopped by finalize(), whose return marks completion.
 * Unsupported runtimes or missing lifecycle symbols return UBSM_ERR_NOT_SUPPORTED.
 * The application must also synchronize direct mapped-memory access.
 */
SHMEM_API int ubsmem_fork_prepare(void);

/**
 * Restore the preparing thread in the original process after fork, fork failure or cancellation.
 * Never waits for the child. On failure ordinary APIs stay BUSY;
 * the preparing thread may retry this function without arguments.
 * When already RUNNING in this process, any ordinary thread may confirm success without IPC.
 * Before initialization or after finalization returns UBSM_ERR_MEMLIB.
 */
SHMEM_API int ubsmem_fork_parent(void);

/**
 * Restore a new child session and discard inherited SDK metadata in place.
 * Old mapping pointers are invalid even before this call. Remap shared memory by name.
 * Does not release/unlock/resume any parent resource. On failure retry child() or _exit().
 * The child must not start other SDK callers until recovery succeeds.
 * Inherited fault subscriptions and external log callbacks are cleared. Register them explicitly again.
 */
SHMEM_API int ubsmem_fork_child(void);

#ifdef __cplusplus
} // end of extern "C"
#endif

#endif // __UBSM_SHMEM_H__
