#!/bin/bash
###################################可配置项#############################################
# 安装包所在目录，默认为脚本所在的上一级目录
PARENT_DIR=$(cd $(dirname $0)/../ ;pwd)

LIB_PATH="/usr/local/ubs_mem/lib/"
NEW_LD_PATH="$LIB_PATH:$LD_LIBRARY_PATH"

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
    exec_cmd export LD_LIBRARY_PATH="$NEW_LD_PATH"
    exec_cmd systemctl start ubsmd.service
    sleep 10
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
        nohup env MXM_CHANNEL_TIMEOUT=120 $PARENT_DIR/bin/matrix_diagnose $param > /ko/matrix_shmem/matrix_diagnose$param.log 2>&1 &
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
