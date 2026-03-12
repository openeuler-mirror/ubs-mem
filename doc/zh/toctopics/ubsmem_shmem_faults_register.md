# ubsmem\_shmem\_faults\_register

## 接口功能

订阅共享内存故障事件，当共享内存发生故障时，会调用注册的事件通知函数。

>[!NOTE]**说明** 
>用户在调用该接口前，需加入ubse属组，并具备ubse的mem.shm类接口权限，具体配置请参见[UBS Engine 配置说明](https://atomgit.com/openeuler/ubs-engine/blob/master/docs/config/%E9%85%8D%E7%BD%AE%E8%AF%B4%E6%98%8E.md)文档。

## 接口格式ffdfrdfcr

```
int ubsmem_shmem_faults_register(shmem_faults_func registerFunc);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|registerFunc|shmem_faults_func|入参|共享内存故障事件响应处理函数。类型定义如下：<pre class="screen">typedef int32_t (*shmem_faults_func)(const char *shm_name);</pre>|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


