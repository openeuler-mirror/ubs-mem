# ubsmem\_lease\_malloc

## 接口功能

申请内存。

>[!NOTE]**说明** 
>同一节点可以借用内存的个数上限为16384。该值为硬件限制下的理论上限，实际还会受限于物理拓扑、物理资源及分配策略。

## 接口格式

```
int ubsmem_lease_malloc(const char *region_name, size_t size, ubsmem_distance_t mem_distance, uint64_t flags, void **local_ptr);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|region_name|const char *|入参|内存域名称（默认域为“default”，包含与当前节点全互联的节点）。节点内唯一标识，最大有效长度为47字符（不包括“\0”），仅允许使用大小写字母、数字、“-”和“_”。|
|size|size_t|入参|申请的内存size。最小4MB，需为4MB整数倍，单位为字节。<br>该参数的最小值与南向依赖UBSE中的配置项[obmm.memory.block.size](https://atomgit.com/openeuler/ubs-engine/blob/master/docs/config/%E9%85%8D%E7%BD%AE%E8%AF%B4%E6%98%8E.md)有关。共享内存以FD的形式进行管理，每个FD管理obmm.memory.block.size大小的内存。共享内存在导出导入时会以该配置项向上取整对齐。<br>例如：该配置项为128MB，创建共享内存时传入size是4MB，实际上会创建出128MB的共享内存。|
|mem_distance|ubsmem_distance_t|入参|节点连接模式，当前仅支持直连借用，即 **DISTANCE_DIRECT_NODE** 取值为0。|
|flags|uint64_t|入参|借用的标志信息。如果为0，默认进行FD借用。当前支持以下flags：<ul><li>UBSM_FLAG_MMAP_HUGETLB_PMD：表示以2MB大页粒度进行映射。<li>UBSM_FLAG_MALLOC_WITH_NUMA：表示借用以远端NUMA呈现。与UBSM_FLAG_MMAP_HUGETLB_PMD不可同时指定，设置该flag借用最小值为128MB。</ul>|
|local_ptr|void **|出参|申请后得到的本地地址。|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


