# 安装UBS Memory

## 前提条件

-   已完成[表2](安装前准备.md#table002)所示的各项依赖的安装。
-   已获取UBS Memory安装包。

## 操作步骤

1.  使用\{UBSM-install-user\}用户登录服务器。
2.  将获取的所有软件包上传到任意目录，并进入该目录。
3.  卸载、安装固件。

    1.  （可选）卸载已存在的UBS Memory、HCOM。

        ```
        rpm -e ubs_mem
        rpm -e ubs-comm-lib-x.x.x
        rpm -e ubs-comm-devel-x.x.x
        ```

    2.  安装HCOM、UBS Memory。

        ```
        rpm -ivh ubs-comm-lib-x.x.x-*rpm
        rpm -ivh ubs-comm-devel-x.x.x-*rpm
        rpm -ivh ubs-mem-memfabric-x.x.x-x.x.*.rpm
        ```

    >[!NOTE]**说明** 
    >- 由于UBS Engine依赖HCOM，卸载HCOM之前需先卸载UBS Engine。
    >- 内存服务以ubsmd用户的身份运行，在使用RPM包安装时，若系统中不存在ubsmd用户，安装脚本将自动创建该用户。
    >- 安装成功后，so会默认安装到“/usr/local/ubs\_mem/lib”目录，使用时需要export该路径。
    >- 安装成功后，.h头文件会默认安装到“/usr/local/ubs\_mem/include”目录。
    >- 配置环境变量 **UBSM\_SDK\_TRACE\_ENABLE = 1**，开启性能打点统计，会在默认的日志路径（/var/log/ubsm）生成对应的打点数据。
    >- 配置环境变量 **MXM\_CHANNEL\_TIMEOUT= xx**，控制IPC通信的channel超时时间（单位s），当大块内存操作耗时较久时，可以配置较长时间，默认为60s。

4.  启动UBS Engine服务。

    ```
    systemctl start ubse.service
    ```

5.  修改ubsmd.conf配置文件。
    1.  打开“/usr/local/ubs\_mem/config/ubsmd.conf”配置文件。

        ```
        vim /usr/local/ubs_mem/config/ubsmd.conf
        ```

    2.  按“i”进入编辑模式，根据实际情况对相关参数进行配置，参数详情请参见[表1](配置参数说明.md#table003)。

        ```
        # the log level of ubsm server, (DEBUG, INFO, WARN, ERROR, CRITICAL)
        ubsm.server.log.level = INFO
        # the log file path, must be canonical path
        ubsm.server.log.path = /var/log/ubsm
        # log file count, min is 1, max is 50
        ubsm.server.log.rotation.file.count = 10
        # log file size(MB), min is 2, max is 100
        ubsm.server.log.rotation.file.size = 20
        # enable or disable audit log, (on, off)
        ubsm.server.audit.enable = on
        # audit log, the configuration item value range is the same as 'ubsm.server.log.*'
        ubsm.server.audit.log.path = /var/log/ubsm
        ubsm.server.audit.log.rotation.file.count = 10
        ubsm.server.audit.log.rotation.file.size = 20
        # CC lock
        ubsm.lock.enable = off
        ubsm.lock.expire.time = 300
        ubsm.lock.dev.name = bonding_dev_0
        ubsm.lock.dev.eid = 0
        # CC lock tls options
        ubsm.lock.tls.enable = on
        ubsm.lock.tls.ca.path = /path/cacert.pem
        ubsm.lock.tls.crl.path = /path/crl.pem
        ubsm.lock.tls.cert.path = /path/cert.pem
        ubsm.lock.tls.key.path = /path/key.pem
        ubsm.lock.tls.keypass.path = /path/keypass.txt
        # Zen discovery
        # election timeout, min is 0, max is 2000
        ubsm.discovery.election.timeout = 1000
        # min nodes, min is 0, max is 30
        ubsm.discovery.min.nodes = 0
        ubsm.server.rpc.local.ipseg = 127.0.0.1:7201
        ubsm.server.rpc.remote.ipseg = 127.0.0.1:7301
        # tls options
        ubsm.server.tls.enable = on
        ubsm.server.tls.ciphersuits = aes_gcm_128
        ubsm.server.tls.ca.path = /path/cacert.pem
        ubsm.server.tls.crl.path = /path/crl.pem
        ubsm.server.tls.cert.path = /path/cert.pem
        ubsm.server.tls.key.path = /path/key.pem
        ubsm.server.tls.keypass.path = /path/keypass.txt
        # max is 8192, default 256
        ubsm.hcom.max.connect.num = 256
        # enable or disable memory lease cache, (on, off)
        ubsm.server.lease.cache.enable = off
        # enable performance statistics, (on off)
        ubsm.performance.statistics.enable = off
        ```

        >[!NOTE]**说明** 
        >- 使用共享内存的分布式锁功能时，需要在配置文件中主动设置当前节点的IP地址和端口号以及集群中其他节点的节点信息，启动当前节点的ubsmd进程，会同步启动其他节点。
        >- 开启TLS（Transport Layer Security，安全传输层协议）认证功能操作详情可参见[开启TLS认证](开启TLS认证.md)，如果不使用该功能，将配置项 **ubsm.server.tls.enable** 设为 **off** 即可。

    3.  按“Esc”键，输入**:wq!**，按“Enter”保存并退出编辑。

6.  启动ubsmd。

    ```
    systemctl start ubsmd
    ```

    >[!NOTE]**说明** 
    >ubsmd进程启动依赖UBSE，该服务启动成功方可加载成功。

7.  查看ubsmd状态。

    ```
    systemctl status ubsmd
    ```

    回显如下表明启动成功：

    ```
    ● ubsmd.service - UBS memory daemon
         Loaded: loaded (/etc/systemd/system/ubsmd.service; enabled; preset: disabled)
         Active: active (running) since xx YYYY-mm-dd HH:MM:SS CST; xxs ago
       Main PID: xxx (ubsmd)
         Status: "available"
          Tasks: 31 (limit: 822900)
       FD Store: 1 (limit: 3)
         Memory: xxG ()
         CGroup: /system.slice/ubsmd.service
                 └─xxx /usr/local/ubs_mem/bin/ubsmd -binpath=/usr/local/ubs_mem
    ```

8.  停止ubsmd。

    >[!NOTE]**说明**  
    >- 内存借用提供类似 **malloc/free** 接口，当应用侧异常退出时，由ubsmd完成资源回收操作。
    >- 内存共享提供类似 **shmem\_allocate/shmem\_deallocate** 接口，应用侧需要跨多节点共享访问，由应用侧负责共享内存的申请释放操作。

    ```
    systemctl stop ubsmd.service
    ```

