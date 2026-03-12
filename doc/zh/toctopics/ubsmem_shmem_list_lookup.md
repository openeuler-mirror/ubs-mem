# ubsmem\_shmem\_list\_lookup

## 接口功能

查询指定前缀的共享内存信息。

## 接口格式

```
int ubsmem_shmem_list_lookup(const char *prefix, ubsmem_shmem_desc_t shm_list[], uint32_t *shm_cnt);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|prefix|const char *|入参|查询的共享前缀名。|
|shm_list|ubsmem_shmem_desc_t []|出参|查到的共享信息，调用方需提前准备一个ubsmem_shmem_desc_t类型的数组。相关结构体和常量定义如下：<pre class="screen">#define MAX_SHM_NAME_LENGTH 48<br><br>typedef struct {<br>char name[MAX_SHM_NAME_LENGTH + 1];<br>size_t size;<br>} ubsmem_shmem_desc_t;</pre>|
|shm_cnt|uint32_t *|出/入参|<ul><li>作为入参，表示shm_list数组的大小。<li>作为出参，表示查到的共享内存个数。</ul>|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


