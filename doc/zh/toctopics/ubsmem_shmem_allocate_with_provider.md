# ubsmem\_shmem\_allocate\_with\_provider

## 接口功能

指定节点创建共享内存对象。

## 接口格式

```
int ubsmem_shmem_allocate_with_provider(const ubs_mem_provider_t *src_loc,, const char *name, size_t size, mode_t mode, uint64_t flags);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|src_loc|const ubs_mem_provider_t *|入参|指定内存创建的节点信息，包括：<ul><li>host_name：节点名称，必填。<li>socket_id：指定内存导出的socket id，选填（UINT32_MAX）。<li>numa_id：指定内存导出的numa id，选填（UINT32_MAX）。<li>port_id：指定内存导出的port id，选填（UINT32_MAX）。</ul>|
|name|const char *|入参|共享内存名称。全局唯一标识，最大有效长度为47字符（不包括“\0”），仅允许使用大小写字母、数字、“.”、“:”、“-”和“_”。|
|size|size_t|入参|共享内存size，最小4MB，需为4MB整数倍，单位为字节。<br>该参数的最小值与南向依赖UBSE中的配置项[obmm.memory.block.size](https://atomgit.com/openeuler/ubs-engine/blob/master/docs/config/%E9%85%8D%E7%BD%AE%E8%AF%B4%E6%98%8E.md)有关。共享内存以FD的形式进行管理，每个FD管理 obmm.memory.block.size 大小的内存。共享内存在导出导入时会以该配置项向上取整对齐。<br>例如：该配置项为128MB，创建共享内存时传入size是4MB，实际上会创建出128MB的共享内存。|
|mode|mode_t|入参|访问权限，Unix文件权限位的 *rwx* 权限控制（ *x* 权限暂不支持，忽略）。|
|flags|uint64_t|入参|创建共享内存的标志信息。flag有效比特位含义请参见[表1](ubsmem_shmem_allocate.md#table005)，各有效比特位组合关系参见[表2](ubsmem_shmem_allocate.md#table006)。|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


