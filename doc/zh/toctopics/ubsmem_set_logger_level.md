# ubsmem\_set\_logger\_level

## 接口功能

设置UBSM打印日志级别。

## 接口格式

```
int ubsmem_set_logger_level(int level);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|level|int|入参|打印日志最高级别，对应级别如下：<ul><li>0：debug（默认）</li><li>1：info</li><li>2：warning</li><li>3：error</li><li>4：critical</li></ul>|


## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|


