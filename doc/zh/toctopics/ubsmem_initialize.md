# ubsmem\_initialize

## 接口功能

UBSM SDK初始化函数，使用UBSM功能前需要调用此函数，依赖UBSM服务。

## 接口格式

```C++
int ubsmem_initialize(const ubsmem_options_t *ubsm_shmem_opts);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ubsm_shmem_opts|ubsmem_options_t *|入参|初始化函数入参，需要先通过ubsmem_init_attributes函数对参数进行初始化。|

## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|
