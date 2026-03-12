# ubsmem\_lookup\_regions

## 接口功能

查询当前节点的相关拓扑信息，如互联节点数以及对应节点的hostname。

## 接口格式

```
int ubsmem_lookup_regions(ubsmem_regions_t *regions);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|regions|ubsmem_regions_t *|出参|互联节点数以及对应节点的hostname。|


相关结构体类型和常量定义。

```
#define MAX_HOST_NAME_DESC_LENGTH 64
#define MAX_REGION_NODE_NUM 16
#define MAX_REGIONS_NUM 6

typedef struct {
  char host_name[MAX_HOST_NAME_DESC_LENGTH];  // 需包含 '\0'
  bool affinity;  // 节点亲和，设置为true时则指定从该节点申请共享或借用内存
} ubsmem_region_node_desc_t;

typedef struct {
  int host_num;
  ubsmem_region_node_desc_t hosts[MAX_REGION_NODE_NUM];
} ubsmem_region_attributes_t;

typedef struct {
  int num;
  ubsmem_region_attributes_t region[MAX_REGIONS_NUM];
} ubsmem_regions_t;
```

## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


