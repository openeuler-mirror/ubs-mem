 # ubs-mem

#### 介绍
UBS Memory在超节点上基于UB硬件能力提供Memory高阶服务能力，实现超节点上的内存借用、共享、缓存等能力。

#### 环境要求
操作系统：推荐 openEuler 24.03 LTS SP3或更高版本
编译工具链： GCC ≥ 10.3

#### 获取源码
```shell
git clone https://atomgit.com/openeuler/ubs-mem.git

cd ubs-mem
```

#### 构建项目
代码仓中提供了统一的编译构建脚本（即build.sh），可以直接执行该脚本编译构建。-p 参数表示打rpm包，-t 表示编译方式如debug、release，示例如下。
```
sh build.sh -t release -p
```

构建产物位于 build/release/output* 目录下，RPM 包输出至 build/release/output。

#### 项目结构
```
.
├── build     // 存放项目中使用的脚本文件
├── doc       // 存放项目文档，例如《代码架构设计》
├── src       // 存放项目的功能实现源码，仅该目录参与构建出包
├── test      // 存放项目的ut和dtfuzz等
└── build.sh  // 统一的构建入口       
```

#### 开发者测试
```
cd test

# 运行特定测试用例
./run_dt.sh -- --gtest_filter="TestShmIpcHandler.*"

# 生成代码覆盖率报告（HTML 可视化）
./run_dt.sh 
# 启动后终端会输出类似：http://localhost:8000/coverage/index.html
```

#### 使用说明
1. 前提：安装并成功启动ubs-engine。
2. 使用rpm  安装我们的rpm包。
3. systemctl start ubsmd 启动。 

#### License
ubs-mem 采用 Mulan V2 License.

#### 贡献指南
请阅读 贡献指南 CONTRIBUTING.md 以了解如何贡献项目。