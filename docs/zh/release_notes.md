# 版本说明书

## 版本配套说明

### 产品版本信息

| 项目 | 内容 |
| ---- | ---- |
| 产品名称 | UBS Memory |
| 产品版本 | master |
| 版本类型 | 正式版本 |

### 相关产品版本配套说明

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

## 版本兼容性说明

无

## 更新说明

### 关键特性变更

- 支持根据Snoop状态做共享内存创建合法性校验。
- HPC场景，ubs-mem支持关闭额外的7个线程，减少对计算程序抖动的影响。

### 接口变更说明

**新增**

- `ubsmem_shmem_ipc_suspend`：支持恢复后台线程及控制功能。
- `ubsmem_shmem_ipc_resume`：支持业务期释放后台线程。

### 已解决的问题

无

### 遗留问题

无

## 升级影响

### 升级过程中对现行系统的影响

- 对业务的影响

    软件版本升级过程中会导致业务中断。

- 对网络通信的影响

    对通信无影响。

### 升级后对现行系统的影响

无

## 版本配套文档

|文档名称|内容简介|
|---|---|
|《[安装部署](../zh/installation_deployment.md)》|提供安装UBS Memory的安装、卸载等操作。|
|《[API接口](../zh/api_description.md)》|提供对外的API。|
|《[安全说明](../zh/security_description.md)》|提供了UBS Memory安全配置相关的内容。|
