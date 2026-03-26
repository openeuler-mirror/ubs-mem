# ubsmem\_lookup\_region

## 接口功能

查询共享域信息。

>[!NOTE]说明
>region仅在创建它的节点上有效，只能在该节点上查询，其他节点无法查询。

## 接口格式

```C++
int ubsmem_lookup_region(const char *region_name, ubsmem_region_desc_t *region_desc);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|region_name|const char *|入参|共享域名称，节点内唯一标识。最大有效长度为47字符（不包括“\0”）。|
|region_desc|ubsmem_region_desc_t *|出参|共享域信息，如域内节点数、对应节点hostname以及亲和性。相关结构体类型和常量定义请参见[参数说明](ubsmem_lookup_regions.md#参数说明)。|

## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|
