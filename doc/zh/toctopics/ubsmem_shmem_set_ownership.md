# ubsmem\_shmem\_set\_ownership

## 接口功能

修改共享内存在本地的权限信息，同时刷新cache。

>[!NOTE]**说明** 
>- 只有已经映射到本地的共享内存才可以调用此接口。
>- 使用NonCache模式的时候，不支持调用修改内存权限接口。

## 接口格式

```
int ubsmem_shmem_set_ownership(const char *name, void *start, size_t length, int prot);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|const char *|入参|通过ubsmem_shmem_allocate创建的共享内存的名称。|
|start|void *|入参|由ubsmem_shmem_map得到的地址。支持刷新某一段地址的共享内存。|
|length|size_t|入参|共享内存的大小。最小值为内核页的大小，且内存地址需与mmap分配的起始地址保持内核页的大小对齐。|
|prot|int|入参|内存权限。当前支持的组合值包含：<ul><li>PROT_NONE</li><li>PROT_READ</li><li>PROT_READ</li></ul>|PROT_WRITE|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


