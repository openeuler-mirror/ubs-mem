# 版本说明书

## 1.1.1

### 版本配套说明

#### 产品版本信息

| 项目 | 内容 |
| ---- | ---- |
| 产品名称 | UBS Memory |
| 产品版本 | 1.1.1 |

#### 相关产品版本配套说明

- 硬件及基础软件要求沿用1.1.0版本的配套说明。
- UBSE客户端及服务端须支持64字节内存对象名称及对应的MMI元数据格式。
- 多节点共享内存池依赖`ubs_mem_shm_list_with_prefix`接口；配套UBSE缺少该接口时，初始化失败。

### 版本兼容性说明

- 共享内存及内存借用对象名称的最大有效长度为64字节，不包括结尾NUL。多节点共享内存池的基础名称
  最大有效长度为60字节，为成员名称后缀预留四字节。
- 名称扩容涉及UBSE描述结构体布局变化，须配套升级UBSE及UBS Memory，并使用匹配的头文件重新编译应用。

### 更新说明

#### 关键特性变更

- 支持由2至256个提供节点组成共享内存池，可均分总容量或分别指定各提供节点的容量。
- 支持按照提供节点顺序连续映射及按照指定粒度交织映射，支持通过现有解除映射接口释放映射。
- 支持池成员完整性、布局一致性及访问权限校验；普通映射和普通删除接口拒绝操作多节点池成员。
- 新增交织映射示例及多节点共享内存池测试工具。

#### 接口变更说明

**新增**

- `ubsmem_shmem_allocate_with_multi_provider`：创建多节点共享内存池。
- `ubsmem_shmem_deallocate_with_multi_provider`：删除多节点共享内存池。
- `ubsmem_shmem_map_with_multi_provider`：按照提供节点顺序连续映射共享内存池。
- `ubsmem_shmem_map_interleaved`：按照指定交织粒度映射共享内存池。
- `UBSM_FLAG_MULTINODE_CUSTOM_SIZE`：使用`ubs_mem_providers_t.host_sizes`指定各提供节点的容量。
- `UBSM_ERR_SHM_POOL_INCOMPLETE`（6026）：共享内存池所需成员缺失。
- `UBSM_ERR_SHM_POOL_LAYOUT_CONFLICT`（6027）：共享内存池成员布局或映射元数据不一致。

**变更**

- `ubsmem_shmem_unmap`支持解除连续映射及交织映射的多节点共享内存池。
- `ubsmem_shmem_set_ownership`支持交织映射的分段地址转换；对于NC及ONLY_IMPORT_NC对象，返回参数错误。

#### 使用约束

- 多提供节点创建接口须显式指定socket平面，不接受`UINT32_MAX`；不支持`UBSM_FLAG_WITH_LOCK`。
- 提供节点数量范围为2至256，拒绝重复主机名或解析至相同nodeId的配置。各节点容量须按4 MiB对齐。
- 交织粒度须满足API文档规定的对齐和整除约束。较小交织粒度会增加VMA数量，映射数量超过
  `vm.max_map_count`限制时可能失败，部署时应按映射规模调整该参数或增大交织粒度。
- 池删除不具备原子性，部分失败后可根据日志重试；缺失成员记录告警并继续清理。

### 升级影响

- 升级前应停止业务并释放现有映射，配套升级相关组件后重新创建或映射内存对象。
- 各接口的参数、返回值及详细约束参见《[API接口](ubsm_api_description.md)》。

## 1.1.0

### 版本配套说明

#### 产品版本信息

| 项目 | 内容 |
| ---- | ---- |
| 产品名称 | UBS Memory |
| 产品版本 | 1.1.0 |
| 版本类型 | 正式版本 |

#### 相关产品版本配套说明

**硬件版本配套表**

| 产品名称 | 版本 |
| --- | --- |
| 服务器 | &#8226; Taishan 500 2280 <br> &#8226; Taishan 950 SuperPod <br> &#8226; 其他配备支持UB的CPU的服务器 |

**软件版本配套表**

| 产品名称 | 版本 |
| --- | --- |
| OS | 推荐 openEuler 24.03 LTS SP3 或更高版本|
| OBMM驱动 | - |
| GCC | ≥ 10.3.1 |
| GCC-C++ | ≥ 10.3.1 |
| C | C99以上 |
| C++ | C++ 17 |
| UBSE软件包：<br> &#8226; ubs-engine <br> &#8226; ubs-engine-client-libs | ≥ 1.0.0 |
| HCOM软件包：<br> &#8226; ubs-comm-lib <br> &#8226; ubs-comm-devel | ≥ 1.0.0 |
| 共享内存的锁功能依赖软件包（可选）：<br>&#8226; umdk-dlock-lib <br> &#8226; umdk-dlock-devel | ≥ 25.12.0 |

### 版本兼容性说明

- `libubsm_sdk.so`新增ABI主版本，ABI主版本为`1`，完整动态库版本号为`1.1.0`，与RPM包的
  `Version`字段保持一致。原动态库路径`/usr/lib64/libubsm_sdk.so`继续保留，已有应用可正常加载。
- SDK头文件由`/usr/include/`目录迁移至`/usr/include/ubs_mem/`目录，使用SDK头文件的应用需要
  调整头文件包含方式或编译搜索路径。

### 更新说明

#### 关键特性变更

- 支持根据Snoop状态做共享内存创建合法性校验。
- HPC场景，ubs-mem支持关闭额外的7个线程，减少对计算程序抖动的影响。
- `libubsm_sdk.so`新增ABI主版本，动态库文件调整为
  `libubsm_sdk.so -> libubsm_sdk.so.1 -> libubsm_sdk.so.1.1.0`。
- SDK头文件统一移动至`/usr/include/ubs_mem/`目录。

#### 接口变更说明

**新增**

- `ubsmem_shmem_ipc_suspend`：支持恢复后台线程及控制功能。
- `ubsmem_shmem_ipc_resume`：支持业务期释放后台线程。

**变更**

| 类型 | 变更前 | 变更后 |
| --- | --- | --- |
| 动态库 | `/usr/lib64/libubsm_sdk.so` | `/usr/lib64/libubsm_sdk.so`<br>`/usr/lib64/libubsm_sdk.so.1`<br>`/usr/lib64/libubsm_sdk.so.1.1.0` |
| 头文件 | `/usr/include/ubs_mem.h` | `/usr/include/ubs_mem/ubs_mem.h` |
| 头文件 | `/usr/include/ubs_mem_def.h` | `/usr/include/ubs_mem/ubs_mem_def.h` |

#### 已解决的问题

无

#### 遗留问题

无

### 升级影响

#### 升级过程中对现行系统的影响

- 对业务的影响

    软件版本升级过程中会导致业务中断。

- 对网络通信的影响

    对通信无影响。

#### 升级后对现行系统的影响

- 若代码通过`dlopen`直接指定`/usr/lib64/libubsm_sdk.so`，功能不受影响。建议使用
  `/usr/lib64/libubsm_sdk.so.1`，以符合动态库ABI版本规范。
- 推荐将SDK头文件包含方式修改为`#include "ubs_mem/ubs_mem.h"`；也可以在编译选项中增加
  `-I/usr/include/ubs_mem`，继续使用原头文件名称。
- 容器中使用宿主机动态库时，建议挂载整个动态库目录，或同时挂载`libubsm_sdk.so`、
  `libubsm_sdk.so.1`和实际文件`libubsm_sdk.so.1.1.0`，确保软链接及其目标文件均可访问。

### 版本配套文档

|文档名称|内容简介|
|---|---|
|《[安装部署](../zh/ubsm_installation_deployment.md)》|提供安装UBS Memory的安装、卸载等操作。|
|《[API接口](../zh/ubsm_api_description.md)》|提供对外的API。|
|《[配置说明](../zh/ubsm_configuration_description.md)》|提供ubsmd.conf配置文件参数说明。|
|《[安全说明](../zh/ubsm_security_description.md)》|提供了UBS Memory安全配置相关的内容。|
