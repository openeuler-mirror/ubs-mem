# ubsmem\_local\_nid\_query

## 接口功能

查询该节点在超节点域中的节点ID。

>[!NOTE]说明
>用户在调用该接口前，需加入ubse属组，并具备ubse的topo类接口权限，具体配置请参见[UBS Engine 配置说明](https://atomgit.com/openeuler/ubs-engine/blob/master/docs/config/%E9%85%8D%E7%BD%AE%E8%AF%B4%E6%98%8E.md)文档。

## 接口格式

```C++
int ubsmem_local_nid_query(uint32_t* node_id);
```

## 参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|node_id|uint32_t*|出参|该节点在超节点域中的节点ID。|

## 返回值

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。具体错误码根据返回值不同参考[错误码](错误码.md)。|
