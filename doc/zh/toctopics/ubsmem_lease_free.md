# ubsmem\_lease\_free

## 接口功能

释放通过ubsmem\_lease\_malloc、ubsmem\_lease\_malloc\_with\_location申请的内存。

## 接口格式

```
int ubsmem_lease_free(void *local_ptr);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|local_ptr|void *|入参|ubsmem_lease_malloc申请的内存指针。|


>[!NOTE]**说明** 
>- 若传入的参数不是通过 ubsmem\_lease\_malloc 申请的内存（即为无效内存地址），系统将检查测并打印告警日志，避免引发系统异常。
>- 若传入的参数是通过 ubsmem\_lease\_malloc 申请的内存，无论内存是否成功释放，该虚拟地址都将无法访问。
>- 释放该段内存时，UBSM不会主动清除其中的数据，需用户自行处理以确保安全性。
>- 若传入NULL，则忽略，并返回相应错误码。

## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


