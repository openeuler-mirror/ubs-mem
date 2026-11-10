# UBS Memory共享内存样例

## 一、Cache模式共享内存样例

### 功能说明

`01_cache_shmem.cpp`使用`UBSM_FLAG_CACHE`创建128MB共享内存，并依次完成：

1. 初始化UBS Memory SDK。
2. 在`default`共享域创建共享内存。
3. 将共享内存以`PROT_NONE`映射到本进程。
4. 获取读写权限并写入数据。
5. 释放权限，通过`ubsmem_shmem_set_ownership(..., PROT_NONE)`刷新并失效cache。
6. 获取只读权限并校验数据。
7. 解除映射、删除共享内存并释放SDK资源。

### 环境要求

- openEuler 24.03 LTS SP3或更高版本。
- 已安装并启动UBS Engine和`ubsmd`。
- 服务器具有可用的UB硬件，且BIOS未配置snoop参数。
- 当前用户属于`ubsmd`和`ubse`用户组，并具有UBSE的`mem.shm`接口权限。
- 已安装UBS Memory SDK头文件和动态库。

### 编译

```shell
cmake -S example -B example/build
cmake --build example/build --target 01_cache_shmem
```

如果SDK安装在非默认目录，通过`UBSM_ROOT`指定安装前缀：

```shell
cmake -S example -B example/build -DUBSM_ROOT=/path/to/ubs_mem
cmake --build example/build --target 01_cache_shmem
```

### 运行

```shell
./example/build/01_cache_shmem
```

程序使用进程ID生成全局共享内存名称，成功时输出读取到的字符串，并在退出前解除映射和删除共享内存。
任一步骤失败时，程序会输出UBS Memory错误码并尽最大努力清理已创建资源。

## 二、共享内存连续拼接样例

### 功能说明

`02_contiguous_shmem.cpp`根据输入的节点数量，将多个128MB共享内存映射到一段连续的虚拟地址空间，
并依次完成：

1. 通过`ubsmem_shmem_allocate_with_provider()`在每个指定节点上创建一个非cache、非接力共享内存。
2. 使用匿名`mmap()`预留一段大小为`host_num * 128MB`的连续虚拟地址空间。
3. 使用`MAP_SHARED | MAP_FIXED`将所有共享内存依次映射到预留空间。
4. 使用不同的字节值对每个分段执行整段`memset()`，再次逐字节遍历所有分段并校验数据，同时输出各分段的
   地址范围。
5. 按相反顺序解除映射、删除共享内存并释放SDK资源。

>[!WARNING]警告
>
>程序先通过`mmap()`申请一段连续的空闲地址，再使用`MAP_FIXED`将多个共享内存依次映射到这段地址中。
>`MAP_FIXED`会先解除目标地址范围内原有的映射。样例中的目标地址是专门为拼接共享内存预留的，请勿使用
>存放其他数据的地址。

### 环境要求

- openEuler 24.03 LTS SP3或更高版本。
- 已安装并启动UBS Engine和`ubsmd`。
- 服务器具有可用的UB硬件。
- 当前用户属于`ubsmd`和`ubse`用户组，并具有UBSE的`mem.shm`接口权限。
- 已安装UBS Memory SDK头文件和动态库。
- 指定的hostname均为可访问的共享内存提供节点，且每个节点至少有128MB可用共享内存。

### 编译

```shell
cmake -S example -B example/build
cmake --build example/build --target 02_contiguous_shmem
```

如果SDK安装在非默认目录，通过`UBSM_ROOT`指定安装前缀：

```shell
cmake -S example -B example/build -DUBSM_ROOT=/path/to/ubs_mem
cmake --build example/build --target 02_contiguous_shmem
```

### 运行

参数格式为`<host_num> <host_0> ... <host_n>`，其中`n = host_num - 1`，`host_num`取值范围为1到16
（`MAX_HOST_NUM`）。后续hostname参数数量必须与`host_num`一致。例如，拼接四个节点提供的共享内存：

```shell
./example/build/02_contiguous_shmem 4 provider-0 provider-1 provider-2 provider-3
```

成功时程序会输出所有共享内存名称和连续的地址范围，例如：

```text
segment[0] 0x7f0000000000-0x7f0008000000: pattern=0x01
segment[1] 0x7f0008000000-0x7f0010000000: pattern=0x02
segment[2] 0x7f0010000000-0x7f0018000000: pattern=0x03
segment[3] 0x7f0018000000-0x7f0020000000: pattern=0x04
```

实际地址由操作系统选择。共享内存名称由进程ID和分段编号生成；样例退出时会尽最大努力解除映射并删除
所有对象。

## 三、多节点内存池交织映射样例

### 功能说明

`03_interleaved_shmem.cpp`只演示交织映射的基本使用流程，不先做普通映射：

1. 根据命令行中的 hostname 顺序创建内存池，每个节点提供 128 MiB，总大小为节点数乘以 128 MiB。
2. 使用 `UBSM_FLAG_CACHE`（CC，值为 0）创建，不设置自定义大小；所有提供节点均指定 `socket_id = 0`。
3. 调用 `ubsmem_shmem_map_interleaved()`，以 4 MiB 粒度、`PROT_READ | PROT_WRITE` 映射整个池。
4. 获取读写 ownership，遍历交织分段，在每段起始位置写入并读回一个 8 字节标记，输出 host、节点内偏移及
   虚拟地址范围；访问结束后释放 ownership。
5. 使用通用的 `ubsmem_shmem_unmap()` 解除映射，再删除池并释放 SDK 资源。

每个节点大小都能被 4 MiB 整除，满足交织映射要求。以两个节点为例，虚拟地址中的顺序为：
`host0[0..4MiB)、host1[0..4MiB)、host0[4..8MiB)、host1[4..8MiB)...`。
输出的 host 对应关系来自接口约定；本例的标记读写不独立验证物理内存来源。

本例固定使用 CC，要求环境支持该模式，并且各提供节点的 socket 0 有足够可用内存。
访问前调用 `ubsmem_shmem_set_ownership(..., PROT_READ | PROT_WRITE)`，访问后调用
`ubsmem_shmem_set_ownership(..., PROT_NONE)` 刷新并失效 cache；数据校验失败时也尝试释放 ownership。
创建使用 `UBSM_FLAG_*`，映射使用 `MAP_SHARED`，两类 flags 不要混用。

### 编译与运行

需要支持多提供节点接口的 UBS Memory SDK，以及 UB 硬件、已部署的 UBSE 和 ubsmd；
权限及环境要求与前面的示例相同。提供 2 到 `MAX_MULTINODE_HOST_NUM`（256）个不同的 hostname，
每个节点至少有 128 MiB 可用共享内存。

```shell
cmake -S example -B build/examples -DUBSM_ROOT=/path/to/ubs_mem
cmake --build build/examples --target 03_interleaved_shmem

# 两个节点各提供 128 MiB，总计 256 MiB，交织粒度 4 MiB
./build/examples/03_interleaved_shmem 2 provider-0 provider-1

# 四个节点，总计 512 MiB
./build/examples/03_interleaved_shmem 4 provider-0 provider-1 provider-2 provider-3
```

程序不等待交互输入，使用进程 ID 和时间戳生成池名称。失败会打印错误码并尝试清理；
如果 unmap 失败，则保留池、打印池名并跳过删除，避免删除仍被使用的对象。
读写验证只触碰每段开头的 8 字节，但仍会实际分配完整内存池，需在 UB 硬件上运行。

原版支持自定义 host 大小、日志级别、flags、交互等待和两种布局整段校验的程序已移动到
[test/tools/multi_provider_shmem_test.cpp](../test/tools/multi_provider_shmem_test.cpp)，
编译与用法见 [测试工具说明](../test/tools/README.md)。

具体部署和权限配置参见[安装部署文档](../docs/zh/ubsm_installation_deployment.md)和
[API文档](../docs/zh/ubsm_api_description.md)。
