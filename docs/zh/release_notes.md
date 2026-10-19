# 版本说明书

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
