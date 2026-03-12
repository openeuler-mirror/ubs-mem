# ubsmem\_init\_attributes

## 接口功能

构造UBSM SDK初始化函数参数，将参数设置为默认值。

## 接口格式

```
int ubsmem_init_attributes(ubsmem_options_t *ubsm_shmem_opts);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ubsm_shmem_opts|ubsmem_options_t *|出参|初始化函数ubsmem_initialize的参数。|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


