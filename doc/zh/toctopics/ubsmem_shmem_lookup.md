# ubsmem\_shmem\_lookup

## 接口功能

根据名称查询指定共享的信息。

## 接口格式

```
int ubsmem_shmem_lookup(const char *name, ubsmem_shmem_info_t *shm_info);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|const char*|入参|查询的共享名。最大有效长度为47字符（不包括“\0”），仅允许使用大小写字母、数字、“-”和“_”。|
|shm_info|ubsmem_shmem_info_t *|出参|对应共享的信息。相关结构体和常量定义如下：<pre class="screen">#define MAX_SHM_NAME_LENGTH 48<br><br>typedef struct {<br>char name[MAX_SHM_NAME_LENGTH + 1];<br>size_t size;<br>} ubsmem_shmem_desc_t>;</pre>|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


