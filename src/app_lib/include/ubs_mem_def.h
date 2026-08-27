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

#ifndef __UBSM_SHMEM_DEF_H__
#define __UBSM_SHMEM_DEF_H__

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SHMEM_API
#define SHMEM_API __attribute__((visibility("default")))
#endif

/** Maximum number of hosts returned in cluster statistics. */
#define MAX_HOST_NUM 16
/** Maximum number of NUMA nodes returned for one host. */
#define MAX_NUMA_NUM 32
/** Size of the reserved extension area in ubsmem_numa_mem_t. */
#define MAX_NUMA_RESV_LEN 16

/** Host-name buffer size, including the terminating null character. */
#define MAX_HOST_NAME_DESC_LENGTH 64
/** Shared-memory name length limit used by the public APIs. */
#define MAX_SHM_NAME_LENGTH 48
/** Region-name buffer size, including the terminating null character. */
#define MAX_REGION_NAME_DESC_LENGTH 48
/** Maximum number of hosts in one region. */
#define MAX_REGION_NODE_NUM 16
/** Maximum number of region topologies returned by ubsmem_lookup_regions(). */
#define MAX_REGIONS_NUM 6
/** Maximum length of an internal OBMM shared-memory device path. */
#define MAX_OBMM_SHMDEV_PATH_LEN 64

/** Maximum number of backing memory IDs returned for one shared-memory object. */
#define MAX_MEMID_NUM 2048
/** Shared-memory list capacity constant retained by the public API. */
#define MAX_SHM_CNT 300

/**
 * ubs-mem operation flags. Shared-memory creation accepts UBSM_FLAG_CACHE through UBSM_FLAG_MMAP_HUGETLB_PMD;
 * lease allocation accepts UBSM_FLAG_MMAP_HUGETLB_PMD or UBSM_FLAG_MALLOC_WITH_NUMA. These values are not the
 * MAP_* flags accepted by ubsmem_shmem_map().
 */
/** Cacheable access mode. This is the default mode because its value is 0. */
#define UBSM_FLAG_CACHE 0x0UL
/** Enable shared-memory lock APIs. Pass this flag alone when using those APIs. */
#define UBSM_FLAG_WITH_LOCK 0x1UL
/** Use non-cacheable access for both the exporter and importers. */
#define UBSM_FLAG_NONCACHE 0x2UL
/** Enable write-delay completion; valid only with a non-cacheable access-mode flag. */
#define UBSM_FLAG_WR_DELAY_COMP 0x4UL
/** Use cacheable access for the exporter and non-cacheable access for importers. */
#define UBSM_FLAG_ONLY_IMPORT_NONCACHE 0X8UL
/** Allow automatic reclamation after all references in the supernode domain are released. */
#define UBSM_FLAG_MEM_ANONYMOUS 0x10UL
/** Map memory with huge-page PMD granularity; supported by shared-memory creation and lease allocation APIs. */
#define UBSM_FLAG_MMAP_HUGETLB_PMD 0x20UL
/** Present leased memory as a remote NUMA node; valid only for lease allocation APIs. */
#define UBSM_FLAG_MALLOC_WITH_NUMA 0x40UL
/** Constant representing 4 KB. */
#define FOUR_KB 4096

/** Public return codes from ubs-mem APIs. */
typedef enum {
    /** Operation completed successfully. */
    UBSM_OK = 0,
    /** Invalid argument. */
    UBSM_ERR_PARAM_INVALID = 6010,
    /** Permission denied. */
    UBSM_ERR_NOPERM = 6011,
    /** Memory operation failed. */
    UBSM_ERR_MEMORY = 6012,
    /** Operation is not implemented. */
    UBSM_ERR_UNIMPL = 6013,
    /** Resource or topology validation failed. */
    UBSM_CHECK_RESOURCE_ERROR = 6014,
    /** Library initialization or runtime operation failed. */
    UBSM_ERR_MEMLIB = 6015,
    /** Operation is unnecessary, such as creating or destroying the reserved default region. */
    UBSM_ERR_NO_NEEDED = 6016,
    /** Resource is busy. */
    UBSM_ERR_BUSY = 6017,
    /** IPC is suspended; resume it before issuing IPC-dependent operations. */
    UBSM_ERR_IPC_SUSPENDED = 6018,
    /** Requested resource was not found. */
    UBSM_ERR_NOT_FOUND = 6020,
    /** Resource already exists. */
    UBSM_ERR_ALREADY_EXIST = 6021,
    /** Memory allocation failed. */
    UBSM_ERR_MALLOC_FAIL = 6022,
    /** Resource record operation failed. */
    UBSM_ERR_RECORD = 6023,
    /** Shared-memory object is still in use. */
    UBSM_ERR_IN_USING = 6024,
    /** Operation or requested mode is not supported. */
    UBSM_ERR_NOT_SUPPORTED = 6025,

    /** Network communication failed. */
    UBSM_ERR_NET = 6040,

    /** UBS Engine operation failed. */
    UBSM_ERR_UBSE = 6050,
    /** OBMM operation failed. */
    UBSM_ERR_OBMM = 6051,

    /** Object does not support shared-memory locking. */
    UBSM_ERR_LOCK_NOT_SUPPORTED = 6060,
    /** Object is already locked by this process. */
    UBSM_ERR_LOCK_ALREADY_LOCKED = 6061,
    /** Distributed lock operation failed. */
    UBSM_ERR_DLOCK = 6062,

    /** Unknown internal error. */
    UBSM_ERR_BUFF = 6099,
} ubsmshmem_ret_t;

/**
 * Physical distance from the current processing element to a memory provider.
 */
typedef enum {
    /** Directly connected provider. This is the only distance currently supported by lease allocation. */
    DISTANCE_DIRECT_NODE = 0,
    /** One-hop provider. Reserved for compatibility and currently not supported by lease allocation. */
    DISTANCE_HOP_NODE = 1,
} ubsmem_distance_t;

/** Library initialization options. Reserved for future options. */
typedef struct {
    /* No configurable options are currently defined. */
} ubsmem_options_t;

/** Description of a host that belongs to a resource region. */
typedef struct {
    /** Null-terminated provider host name. */
    char host_name[MAX_HOST_NAME_DESC_LENGTH];
    /** Whether allocation should prefer this host. */
    bool affinity;
} ubsmem_region_node_desc_t;

/** Host membership and affinity attributes for one resource region. */
typedef struct {
    /** Number of valid entries in hosts. */
    int host_num;
    /** Hosts belonging to the region. */
    ubsmem_region_node_desc_t hosts[MAX_REGION_NODE_NUM];
} ubsmem_region_attributes_t;

/** Resource region topology consisting of the local node and all directly connected nodes. */
typedef struct {
    /** Number of valid entries in region. */
    int num;
    /** Available region topologies. */
    ubsmem_region_attributes_t region[MAX_REGIONS_NUM];
} ubsmem_regions_t;

/** Description of a named resource region. */
typedef struct {
    /** Null-terminated region name. */
    char region_name[MAX_REGION_NAME_DESC_LENGTH];
    /** Region quota. Currently always 0 because quotas are not supported. */
    size_t size;
    /** Hosts and affinity attributes in the region. */
    ubsmem_region_attributes_t region_attr;
} ubsmem_region_desc_t;

/** Memory statistics for one NUMA node. */
typedef struct {
    /** Unique node slot ID. */
    uint32_t slot_id;
    /** Socket ID containing the NUMA node. */
    uint32_t socket_id;
    /** NUMA node ID. */
    uint32_t numa_id;
    /** Upper limit on the percentage of pooled memory that can be lent. */
    uint32_t mem_lend_ratio;
    /** Total memory in bytes. */
    uint64_t mem_total;
    /** Free memory in bytes. */
    uint64_t mem_free;
    /** Borrowed memory in bytes. */
    uint64_t mem_borrow;
    /** Lent memory in bytes. */
    uint64_t mem_lend;
    /** Reserved for future extensions. */
    uint8_t resv[MAX_NUMA_RESV_LEN];
} ubsmem_numa_mem_t;

/** Cluster memory statistics for one host. */
typedef struct {
    /** Null-terminated host name. */
    char host_name[MAX_HOST_NAME_DESC_LENGTH];
    /** Number of valid entries in numa. */
    int numa_num;
    /** Per-NUMA memory statistics. */
    ubsmem_numa_mem_t numa[MAX_NUMA_NUM];
} ubsmem_host_info_t;

/** Memory statistics for hosts visible in the cluster. */
typedef struct {
    /** Number of valid entries in host. */
    int host_num;
    /** Per-host memory statistics. */
    ubsmem_host_info_t host[MAX_HOST_NUM];
} ubsmem_cluster_info_t;

/** Summary of a named shared-memory object returned by a list query. */
typedef struct {
    /** Null-terminated shared-memory object name. */
    char name[MAX_SHM_NAME_LENGTH + 1];
    /** Object size in bytes. */
    size_t size;
} ubsmem_shmem_desc_t;

/** Detailed backing-memory information for a shared-memory object. */
typedef struct {
    /** Null-terminated shared-memory object name. */
    char name[MAX_SHM_NAME_LENGTH + 1];
    /** Object size in bytes. */
    size_t size;
    /** Number of valid entries in mem_id_list. */
    uint32_t mem_num;
    /** Size in bytes represented by each backing memory ID. */
    uint64_t mem_unit_size;
    /** Backing memory IDs. */
    uint64_t mem_id_list[MAX_MEMID_NUM];
} ubsmem_shmem_info_t;

/** Shared-memory UB fault event type. */
typedef enum {
    UBMEM_ATOMIC_DATA_ERR = 0,
    UBMEM_READ_DATA_ERR,
    UBMEM_FLOW_POISON,
    UBMEM_FLOW_READ_AUTH_POISON,
    UBMEM_FLOW_READ_AUTH_RESPERR,
    UBMEM_TIMEOUT_POISON,
    UBMEM_TIMEOUT_RESPERR,
    UBMEM_READ_DATA_POISON,
    UBMEM_READ_DATA_RESPERR,
    UBMEM_MAR_NOPORT_VLD_INT_ERR,
    UBMEM_MAR_FLUX_INT_ERR,
    UBMEM_MAR_WITHOUT_CXT_ERR,
    UBMEM_RSP_BKPRE_OVER_TIMEOUT_ERR,
    UBMEM_MAR_NEAR_AUTH_FAIL_ERR,
    UBMEM_MAR_FAR_AUTH_FAIL_ERR,
    UBMEM_MAR_TIMEOUT_ERR,
    UBMEM_MAR_ILLEGAL_ACCESS_ERR,
    UBMEM_REMOTE_READ_DATA_ERR_OR_WRITE_RESPONSE_ERR,
    /** The shared-memory object is healthy. */
    UBMEM_HEALTHY = 1000,
} ubsmem_fault_type_t;

/** Callback invoked for a shared-memory UB fault event. */
typedef int32_t (*shmem_faults_func)(const char *shm_name, ubsmem_fault_type_t fault_type);

/** Exact provider topology for ubsmem_lease_malloc_with_location(). */
typedef struct {
    /** Unique provider node slot ID. */
    uint32_t slot_id;
    /** Provider socket ID. */
    uint32_t socket_id;
    /** Provider NUMA node ID. */
    uint32_t numa_id;
    /** Provider port ID selecting the borrowing link. */
    uint32_t port_id;
} ubs_mem_location_t;

/** Provider selector for ubsmem_shmem_allocate_with_provider(). */
typedef struct {
    /** Required null-terminated provider host name. */
    char host_name[MAX_HOST_NAME_DESC_LENGTH];
    /** Provider socket ID, or UINT32_MAX when unspecified. */
    uint32_t socket_id;
    /** Provider NUMA node ID, or UINT32_MAX when unspecified. */
    uint32_t numa_id;
    /** Provider port ID, or UINT32_MAX when unspecified. */
    uint32_t port_id;
} ubs_mem_provider_t;

#ifdef __cplusplus
}
#endif

#endif // __UBSM_SHMEM_DEF_H__
