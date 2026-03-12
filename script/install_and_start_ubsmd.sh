#!/bin/bash
###################################可配置项#############################################
# 安装包所在目录，默认为脚本所在的上一级目录
PARENT_DIR=$(cd $(dirname $0)/../ ;pwd)

MATRIX_MEM_NAME=ubs_mem-memfabric-1.0.0-1.oe2203sp13.aarch64.rpm
#rpm安装后的路径
MATRIX_ENGINE_INSTALL_PATH=/usr/local/ubs_mem/
# 运行时的环境变量
MATRIX_ENVIRONMENT_VARIABLE=$MATRIX_ENGINE_INSTALL_PATH/lib/:$RESULT_REMOTE_PATH/lib

# 优化后的配置文件更新函数
load_conf() {
    local config_path="${MATRIX_ENGINE_INSTALL_PATH}/config/ubsmd.conf"
    # 检查配置文件是否存在
    if [ ! -f "$config_path" ]; then
        echo "Error: Configuration file not found at $config_path"
        exit 1
    fi
    # 更新配置项
    sed -i "s/ubsm.lock.enable *=.*$/ubsm.lock.enable = off/g" "$config_path"
    sed -i "s/ubsm.server.tls.enable *=.*$/ubsm.server.tls.enable = off/g" "$config_path"
    sed -i "s/ubsm.lock.tls.enable *=.*$/ubsm.lock.tls.enable = off/g" "$config_path"
}

exec_cmd() {
    echo "[INFO][${FUNCNAME[1]}] command is: $@"
    $@
    if [ $? -eq 0 ]; then
        echo "[INFO][${FUNCNAME[1]}] command execute success"
    else
        echo "[ERROR][${FUNCNAME[1]}] fail, exit code is <$?>!"
        exit 1
    fi
}

install_and_start_mxmd(){
    pkill -9 matrix_diagnose || true
    pkill -9 matrix_mem_test || true
    rm -rf /tmp/*.lock
    rpm -e ubs_mem || true
    exec_cmd rpm -ivh $PARENT_DIR/$MATRIX_MEM_NAME --force --nodeps
    load_conf
    exec_cmd systemctl start ubsmd.service
    sleep 5
    echo "start cli_server..."
    $PARENT_DIR/bin/cli_server > /dev/null 2>&1 &
}

start_server(){
    echo "start server..."
    params=()
    for ((i = 123; i <= 124; i += 1)); do
        params+=($i)
    done
    for param in "${params[@]}"; do
        nohup $PARENT_DIR/bin/matrix_diagnose $param > /ko/matrix_shmem/matrix_diagnose$param.log 2>&1 &
        if [ $? -eq 0 ]; then
            echo "[INFO][${FUNCNAME[1]}] command execute success"
        else
            echo "[ERROR][${FUNCNAME[1]}] fail, exit code is <$?>!"
            exit 1
        fi
    done
}

install_and_start_mxmd
start_server

exit 0
