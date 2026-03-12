# ubsmem\_create\_region

## 接口功能

创建共享域。

>[!NOTE]**说明** 
>region仅在其创建的节点上有效，并且只能在该节点上使用。

## 接口格式

```
int ubsmem_create_region(const char *region_name, size_t size, const ubsmem_region_attributes_t *reg_attr);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|region_name|char *|入参|共享域名称，节点内唯一标识。最大有效长度为47字符（不包括“\0”）。|
|size|size_t|入参|控制资源使用，当前不支持quota，必须填0。|
|reg_attr|ubsmem_region_attributes_t *|入参|指定属于该共享域的节点以及亲和节点，亲和节点用于创建指定节点的共享内存。相关结构体类型和常量定义请参见[参数说明](ubsmem_lookup_regions.md#参数说明)。|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


