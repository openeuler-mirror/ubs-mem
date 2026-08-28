# UBS Memory共享内存样例

`cache_shmem_example.cpp`演示如何使用`UBSM_FLAG_CACHE`创建128MB共享内存，并依次完成：

1. 初始化UBS Memory SDK。
2. 在`default`共享域创建共享内存。
3. 将共享内存以`PROT_NONE`映射到本进程。
4. 获取读写权限并写入数据。
5. 释放权限，通过`ubsmem_shmem_set_ownership(..., PROT_NONE)`刷新并失效cache。
6. 获取只读权限并校验数据。
7. 解除映射、删除共享内存并释放SDK资源。

## 环境要求

- openEuler 24.03 LTS SP3或更高版本。
- 已安装并启动UBS Engine和`ubsmd`。
- 服务器具有可用的UB硬件，BIOS未配置snoop参数；`UBSM_FLAG_CACHE`为CC模式，不支持开启snoop的环境。
- 当前用户属于`ubsmd`和`ubse`用户组，并具有UBSE的`mem.shm`接口权限。
- 已安装UBS Memory SDK头文件和动态库。RPM默认安装到`/usr/include`和`/usr/lib64`，DEB使用系统multiarch目录。

具体部署和权限配置参见[安装部署文档](../docs/zh/installation_deployment.md)和
[API文档](../docs/zh/api_description.md)。

## 编译

使用默认安装路径：

```shell
cmake -S example -B example/build
cmake --build example/build
```

如果SDK安装在其他目录，通过`UBSM_ROOT`指定其安装前缀。该目录下必须包含`include/ubs_mem.h`和
`lib/libubsm_sdk.so`：

```shell
cmake -S example -B example/build -DUBSM_ROOT=/path/to/ubs_mem
cmake --build example/build
```

## 运行

```shell
export HCOM_CONNECTION_RETRY_TIMES=2
./example/build/cache_shmem_example
```

程序使用进程ID生成全局共享内存名称，成功时输出读取到的字符串，并在退出前解除映射和删除共享内存。
任一步骤失败时，程序会输出UBS Memory错误码并尽最大努力按相反顺序清理已创建资源。
