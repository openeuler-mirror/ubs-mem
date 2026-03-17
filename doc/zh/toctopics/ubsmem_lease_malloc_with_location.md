# ubsmem\_lease\_malloc\_with\_location

## 接口功能

申请指定节点的内存。

## 接口格式

```
int ubsmem_lease_malloc_with_location(const ubs_mem_location_t *src_loc, size_t size, uint64_t flags, void **local_ptr);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|src_loc|const ubs_mem_location_t *|入参|指定借出内存节点的信息，包括slot_id、socket_id、numa_id和port_id。|
|size|size_t|入参|申请的内存size。最小4MB，需为4MB整数倍，单位为字节。<br>该参数的最小值与南向依赖UBSE中的配置项[obmm.memory.block.size](https://atomgit.com/openeuler/ubs-engine/blob/master/docs/config/%E9%85%8D%E7%BD%AE%E8%AF%B4%E6%98%8E.md)有关。共享内存以FD的形式进行管理，每个FD管理obmm.memory.block.size大小的内存。共享内存在导出导入时会以该配置项向上取整对齐。<br>例如：该配置项为128MB，创建共享内存时传入size是4MB，实际上会创建出128MB的共享内存。|
|flags|uint64_t|入参|借用的标志信息。如果为0，默认进行FD借用。当前支持以下flags：<ul><li>UBSM_FLAG_MMAP_HUGETLB_PMD：表示以2MB大页粒度进行映射。</li><li>UBSM_FLAG_MALLOC_WITH_NUMA：表示借用以远端NUMA呈现。与UBSM_FLAG_MMAP_HUGETLB_PMD不可同时指定，设置该flag借用最小值为128MB。</li></ul>|
|local_ptr|void **|出参|申请后得到的本地地址。|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


