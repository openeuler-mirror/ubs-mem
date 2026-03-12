# 卸载UBS Memory

## 操作步骤

1.  使用\{UBSM-install-user\}用户登录服务器。
2.  卸载UBS Memory。

    >[!CAUTION]**注意** 
    >- 卸载会自动停止ubsmd并释放占用的远端内存，因此在卸载ubsmd时，应确保ubse正常运行且无业务正在进行。
    >- 为了避免权限问题，卸载后用户和用户组ubsmd将会保留。
    >- 如需卸载UBS Engine，请参见[UBS Engine 部署说明](https://atomgit.com/openeuler/ubs-engine/blob/master/docs/build_install/%E9%83%A8%E7%BD%B2%E8%AF%B4%E6%98%8E.md)文档。

    ```
    rpm -e ubs_mem
    ```

