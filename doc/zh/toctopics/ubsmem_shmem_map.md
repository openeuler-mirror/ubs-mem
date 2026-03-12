# ubsmem\_shmem\_map

## 接口功能

映射共享内存对象到本进程地址空间。

>[!NOTE]**说明** 
>- 同一节点可以映射的共享内存的个数上限为18432，该值为硬件限制下的理论上限，实际还会受限于物理拓扑、物理资源及分配策略。
>- 应用进程在每次映射操作中会占用一定数量的文件描述符（FD，File Descriptor），系统默认的FD上限为1024，因此在频繁映射场景下，存在触及FD限制而导致系统映射失败的风险，需要按需修改系统配置。

## 接口格式

```
int ubsmem_shmem_map(void *addr, size_t length, int prot, int flags, const char *name, off_t offset, void **local_ptr);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|addr|void *|入参|指定期望的地址。|
|length|size_t|入参|映射长度。当前仅支持整个共享内存映射，此值必须为共享内存size。|
|prot|int|入参|映射内存权限。当前支持的组合值包含：<ul><li>PROT_NONE<li>PROT_READ<li>PROT_READ</ul>|PROT_WRITE|
|flags|int|入参|引用系统mmap的取值，可选如下参数：<ul><li>MAP_SHARED<li>MAP_PRIVATE<li>MAP_FIXED<li>MAP_FIXED_NOREPLACE</ul>|
|name|const char *|入参|通过ubsmem_shmem_allocate创建的共享内存的名称。|
|offset|off_t|入参|映射的起始偏移。当前仅支持整个共享内存映射，此值必须为0。|
|local_ptr|void **|出参|映射成功时得到的本地地址。|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


