# ubsmem\_shmem\_allocate

## 接口功能

创建共享内存对象。

>[!NOTE]说明
>
>- 当用户的进程绑定了指定的NUMA节点时，系统会从具有NUMA亲和性的节点申请共享内存。
>- 如果在指定的NUMA节点上申请内存失败，系统会尝试从其他节点申请内存。若希望通过绑定特定NUMA节点来提升性能，需及时判断内存是否成功从目标节点分配。

## 接口格式

```C++
int ubsmem_shmem_allocate(const char *region_name, const char *name, size_t size, mode_t mode, uint64_t flags);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|region_name|const char *|入参|内存域名称（默认域为“default”，包含与当前节点全互联的节点）。|
|name|const char *|入参|共享内存名称。全局唯一标识，最大有效长度为47字符（不包括“\0”），仅允许使用大小写字母、数字、“.”、“:”、“-”和“_”。|
|size|size_t|入参|共享内存size，最小4MB，需为4MB整数倍，单位为字节。<br>该参数的最小值与南向依赖UBSE中的配置项[obmm.memory.block.size](https://atomgit.com/openeuler/ubs-engine/blob/master/docs/config/%E9%85%8D%E7%BD%AE%E8%AF%B4%E6%98%8E.md)有关。共享内存以FD的形式进行管理，每个FD管理 obmm.memory.block.size 大小的内存。共享内存在导出导入时会以该配置项向上取整对齐。<br>例如：该配置项为128MB，创建共享内存时传入size是4MB，实际上会创建出128MB的共享内存。|
|mode|mode_t|入参|访问权限，Unix文件权限位的 *rwx* 权限控制（ *x* 权限暂不支持，请忽略）。用于控制不同用户对该共享内存的访问权限，若无权限则映射失败。建议值： **S_IRUSR \| S_IWUSR** （即仅创建该共享内存的用户可以访问该共享内存）。|
|flags|uint64_t|入参|创建共享内存的标志信息。flag有效比特位含义请参见[表1](#table005)，各有效比特位组合关系参见[表2](#table006)。|

**表 1 <a id="table005"></a>**  共享内存的flags详情

|flag|描述|说明|
|--|--|--|
|UBSM_FLAG_CACHE|表示是否支持cache模式。|该模式需要配合ubsmem_shmem_set_ownership接口对共享内存进行读写操作，同时，在BIOS的启动参数中不能配置snoop参数。|
|UBSM_FLAG_NONCACHE|表示是否开启NonCache模式。|该模式需要配合非接力，否则不保证数据一致性。|
|UBSM_FLAG_WR_DELAY_COMP|表示在提供共享内存时，开启非接力模式。|-|
|UBSM_FLAG_ONLY_IMPORT_NONCACHE|表示是否开启半NonCache模式，该模式下共享的内存导入方使用NonCache模式，导出方使用Cacheable模式。|该模式下，需要在BIOS的启动参数中配置snoop参数。|
|UBSM_FLAG_MEM_ANONYMOUS|带有该flag的共享内存，在一段时间内（最少5min）没有任何使用方时，UBSE会自动将其回收。|-|
|UBSM_FLAG_MMAP_HUGETLB_PMD|带有该flag的共享内存，映射时会以2MB大页为粒度进行。|仅支持指定UBSM_FLAG_ONLY_IMPORT_NONCACHE的情况下使用该flag。|

**表 2 <a id="table006"></a>**  共享内存的flags可组合关系

|**flag**|UBSM_FLAG_CACHE|UBSM_FLAG_NONCACHE|UBSM_FLAG_WR_DELAY_COMP|UBSM_FLAG_ONLY_IMPORT_NONCACHE|UBSM_FLAG_MEM_ANONYMOUS|UBSM_FLAG_MMAP_HUGETLB_PMD|
|--|--|--|--|--|--|--|
|UBSM_FLAG_CACHE|-|-|-|-|**√**|-|
|UBSM_FLAG_NONCACHE|-|-|**√**|-|**√**|-|
|UBSM_FLAG_WR_DELAY_COMP|-|**√**|-|**√**|-|-|
|UBSM_FLAG_ONLY_IMPORT_NONCACHE|-|-|**√**|-|**√**|**√**|
|UBSM_FLAG_MEM_ANONYMOUS|**√**|**√**|-|**√**|-|-|
|UBSM_FLAG_MMAP_HUGETLB_PMD|-|-|-|**√**|-|-|

## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|
