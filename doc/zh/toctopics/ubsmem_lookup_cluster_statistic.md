# ubsmem\_lookup\_cluster\_statistic

## 接口功能

查询集群节点内存信息。

## 接口格式

```C++
int ubsmem_lookup_cluster_statistic(ubsmem_cluster_info_t* info);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|info|ubsmem_cluster_info_t*|出参|集群的节点内存信息。相关结构体和常量定义如下。|

结构体和常量定义：

```C++
#define MAX_HOST_NUM 16
#define MAX_NUMA_NUM 32
#define MAX_NUMA_RESV_LEN 16
#define MAX_HOST_NAME_DESC_LENGTH 64

typedef struct {
  uint32_t slot_id;    // 节点唯一标识，采用slot id，与UBS Eengine一致
  uint32_t socket_id;
  uint32_t numa_id;    
  uint32_t mem_lend_ratio;  // 池化内存借出比例上限
  uint64_t mem_total;       // 节点内存总量，单位字节
  uint64_t mem_free;        // 内存空闲量，单位字节
  uint64_t mem_borrow;      // 借用的内存量，单位字节
  uint64_t mem_lend;        // 借出的内存量，单位字节
  uint8_t resv[MAX_NUMA_RESV_LEN];
} ubsmem_numa_mem_t;

typedef struct {
  char host_name[MAX_HOST_NAME_DESC_LENGTH];
  int numa_num;
  ubsmem_numa_mem_t numa[MAX_NUMA_NUM];
} ubsmem_host_info_t;

typedef struct {
  int host_num;   // Number of available nodes in the cluster
  ubsmem_host_info_t host[MAX_HOST_NUM];
} ubsmem_cluster_info_t;
```

## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|
