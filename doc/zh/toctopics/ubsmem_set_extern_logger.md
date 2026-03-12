# ubsmem\_set\_extern\_logger

## 接口功能

自定义UBSM日志打印函数。

## 接口格式

```
int ubsmem_set_extern_logger(void (*func)(int level, const char *msg));
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|func|void (*)(int level, const char *msg)|入参|UBSM组件日志定向打印函数。|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


