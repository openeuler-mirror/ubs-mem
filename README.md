# ubs-mem

### 介绍
UBS Memory(Unified Bus Service Core Memory)在超节点上基于UB硬件能力提供Memory高阶服务能力，实现超节点上的内存借用、共享、缓存等能力。

### 环境要求

操作系统：推荐 openEuler 24.03 LTS SP3或更高版本

架构：项目代码不限制编译架构，支持使用目标操作系统提供的 x86_64 或 aarch64 工具链构建。aarch64
环境下，单元测试脚本会自动为 mockcpp 应用指令跳转和缓存刷新补丁。

### 软件要求

**构建工具：**

- `rpm-build`：用于生成 RPM 安装包
- `git`：用于获取源码和生成 RPM 源码归档
- `cmake` (≥ 3.13)：跨平台构建系统
- `ninja-build`：高速构建工具，替代 make

**编译环境：**

- `gcc`(≥ 10.3.1)：C 语言编译器
- `gcc-c++`(≥ 10.3.1)：C++ 语言编译器

**依赖库：**

- `numactl-devel`: NUMA (Non-Uniform Memory Access) 支持库
- `systemd-devel`: systemd 服务管理支持库
- `libboundscheck`: 安全函数库，提供 `/usr/include/securec.h`，包含 `memcpy_s` 等安全 C 函数接口。
- `ubs-comm-devel`: UBS 通信库开发包，提供 `/usr/include/hcom/hcom_service.h` 和 HCOM 链接库。

在 openEuler 24.03 LTS SP3 或更高版本中，可以执行以下命令安装构建依赖：

```shell
dnf install -y rpm-build git make cmake gcc gcc-c++ ninja-build \
    numactl-devel systemd-devel libboundscheck ubs-comm-devel
```

`ubs-comm-devel` 会依赖安装运行时包 `ubs-comm-lib`。项目使用 `hcom_service.h`，库文件默认安装在 `/usr/lib64`。
如果当前软件源不提供上述软件包，请参考
[UBS Comm](https://gitcode.com/openeuler/ubs-comm) 项目获取源码并构建安装。

### 获取源码
```shell
git clone https://gitcode.com/openeuler/ubs-mem.git

cd ubs-mem
```

### 构建项目
代码仓中提供了统一的编译构建脚本 `build.sh`。使用 `-t` 指定 debug、release 等直接编译类型：

```shell
sh build.sh -t release
```

构建默认使用当前可用 CPU 核心数的 50%（至少 1 个任务）。可通过 `--jobs`（或 `-j`）指定并发数，
也可以使用 `BUILD_JOBS` 环境变量；命令行参数优先级更高：

```shell
sh build.sh -t release --jobs 8
BUILD_JOBS=8 sh build.sh -t release
```

直接编译产物位于 `build/release/output`。生成 RPM 包时执行：

```shell
sh build.sh -p
```

`-p` 会通过 `ubs-mem.spec` 执行独立的 RelWithDebInfo 构建，因此与 `-t` 同时使用时会忽略 `-t`。
RPM 包输出至 `build/rpm`。

### 项目结构
```text
.
├── build     // 存放项目中使用的脚本文件
├── doc       // 存放项目文档，例如《代码架构设计》
├── src       // 存放项目的功能实现源码，仅该目录参与构建出包
├── test      // 存放项目的ut和dtfuzz等
└── build.sh  // 统一的构建入口
```

### 开发者测试

运行单元测试前需要安装 `git`、`patch`、`libasan`、`openssl` 和 `openssl-devel`：

```shell
dnf install -y git patch libasan openssl openssl-devel
```

首次执行 `sh run_dt.sh` 时，脚本会联网拉取 `googletest` 和 `mockcpp` Git 子模块；请确保能够访问
`.gitmodules` 中配置的仓库地址。

```shell
cd test

# 编译并运行全部 UT，默认不生成覆盖率报告
sh run_dt.sh

# 发生首个失败时立即停止
sh run_dt.sh --fast

# 仅编译 UT
sh run_dt.sh --skip-run-tests

# 指定编译并发数；默认使用当前可用 CPU 的 60%
sh run_dt.sh --jobs 8
```

覆盖率统计默认关闭。如系统已安装 `lcov` 和 `genhtml`，可执行 `sh run_dt.sh --coverage`。详细报告位于
`test/build/gcovr_report/index.html`。

### 使用说明
- **安装部署**
    安装部署相关内容请参见 [安装部署](docs/zh/installation_deployment.md)。
- **API接口**
    API相关内容请参见 [接口说明](docs/zh/api_description.md)。
    文档中的共享内存和内存借用样例依赖 UB 硬件、UBS Engine 及已部署的 ubsmd 服务，不是脱离物理环境的独立样例。
- **共享内存样例**
    `UBSM_FLAG_CACHE` 模式下创建、映射、读写和释放 128MB 共享内存的完整样例请参见
    [example/README.md](example/README.md)。

### License
ubs-mem 采用 Mulan V2 License.

### 贡献指南
请阅读[贡献指南](CONTRIBUTING.md)以了解如何贡献项目。
