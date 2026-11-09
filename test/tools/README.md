# 多提供节点共享内存测试工具

`multi_provider_shmem_test.cpp`由原 `example/03_multi_provider_shmem.cpp` 迁移，
用于硬件环境下的功能验证，不属于自动运行的单元测试。

程序先按 host 顺序普通映射池，对每个 host 对应的全部内存写入不同字节值，再解除映射，
以 4 MiB 粒度交织映射同一个池并逐字节校验。支持不等长 host，耗尽的 host 跳过。
NONCACHE/ONLY_IMPORT_NONCACHE 模式直接访问；其他 cache 模式在访问前后切换 ownership。
创建、普通映射、交织映射成功后都等待输入 `c`；输入关闭时进入清理流程。

## 编译

在仓库根目录执行，需具备项目构建依赖：

```shell
cmake -S . -B build/multi-provider-test -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TEST=ON -DDEBUG_UT=OFF -DDEBUG_FUZZ=OFF -DENABLE_PTRACER=OFF
cmake --build build/multi-provider-test --target multi_provider_shmem_test --parallel 8
```

程序位于 `build/multi-provider-test/test/tools/multi_provider_shmem_test`。
运行时确保动态库加载路径包含配套 SDK 及其依赖；不要与旧版 SDK 混用。
执行 `install` 目标时，工具安装到构建目录的 `output_test/bin`。

## 运行

需要 UB 硬件、运行中的 UBSE 和 ubsmd，以及与示例相同的接口权限。
每个提供节点须有指定大小的可用内存。

```shell
# 默认每 host 128 MiB，NONCACHE，SDK 日志级别 info
./build/multi-provider-test/test/tools/multi_provider_shmem_test 2 provider-0 provider-1

# 自定义大小，以 MiB 为单位，自动启用 CUSTOM_SIZE
./build/multi-provider-test/test/tools/multi_provider_shmem_test \
    --log-level 0 --flags 0x2 2 provider-0 128 provider-1 256

# cache 模式
./build/multi-provider-test/test/tools/multi_provider_shmem_test \
    --log-level 1 --flags 0 2 provider-0 64 provider-1 128
```

选项必须放在节点数前。日志级别：0=debug、1=info、2=warning、3=error、4=critical。
`--flags` 接受十进制或 `0x` 十六进制创建 flags，不是映射时的 `MAP_*`。
host 数量为 2..256，hostname 不能重复；显式大小必须为正数且按 4 MiB 对齐。
多提供节点创建接口必须显式指定 socket 平面，不接受 UINT32_MAX；本工具统一指定 socket 0。
不支持 WITH_LOCK；不支持 MEM_ANONYMOUS，因为首次解除映射后还要重新映射同一个池。

此工具会遍历整个池，内存需求及耗时随 host 数和大小增长。清理时 unmap 最多尝试两次；
仍失败则保留池并打印名称，不直接删除仍在使用的对象。
简洁的交织映射用法见 [03 示例](../../example/03_interleaved_shmem.cpp)。
