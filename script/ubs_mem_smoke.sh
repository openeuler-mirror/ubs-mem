#!/bin/bash
###################################可配置项#############################################
# 安装包所在目录，默认为脚本所在的上一级目录
PARENT_DIR=$(cd $(dirname $0)/../ ;pwd)

LIB_PATH="/usr/local/ubs_mem/lib/"
CONTROLLER_PATH="/usr/local/softbus/ctrlbus/lib"
NEW_LD_PATH="$LIB_PATH:$LD_LIBRARY_PATH:$CONTROLLER_PATH"

MATRIX_MEM_NAME=ubs_mem-memfabric-1.0.0-1.oe2203sp13.aarch64.rpm
UBSE_NAME=ubse-1.0.0-1.aarch64.rpm
TEST_ZIP_NAME=ubs_mem_test.zip

MATRIX_MEM_PATH=$PARENT_DIR/$MATRIX_MEM_NAME
echo "rpm path: $MATRIX_MEM_PATH"
TEST_ZIP_PATH=$PARENT_DIR/$TEST_ZIP_NAME
TEST_PATH=$PARENT_DIR/ubs_mem_test/bin

UB_REMOTE_PATH=/ko/matrix_shmem/

#######################################################################################

#默认UB环境，请勿手动修改，会根据入参修改下面的值
ENVIRONMENT=UB
RESULT_REMOTE_PATH=$UB_REMOTE_PATH

#rpm安装后的路径
MATRIX_ENGINE_INSTALL_PATH=/usr/local/ubs_mem/
MEM_FABRIC_INSTALL_PATH=/usr/local/
# 运行时的环境变量
MATRIX_ENVIRONMENT_VARIABLE=$MATRIX_ENGINE_INSTALL_PATH/lib/:$RESULT_REMOTE_PATH/lib
#UDS路径
MATRIX_UDS_PATH=/run/matrix/memory
MATRIX_LOG_PATH=/var/log/ubsm/
MATRIX_ENGINE_LOG_PATH="/var/log/scbus/"
MEM_FABRIC_LOG_FILE="$MATRIX_ENGINE_LOG_PATH/rackmem_manager.log"

# 用于锁模块初始化配置
IP1="192.168.100.100"
IP2="192.168.100.101"

uvs_ip=$(hostname -I | tr ' ' '\n' | grep -E '192\.168\.100\.[0-9]+')
CURRENT_UVS_IP="$uvs_ip"

if [ "$CURRENT_UVS_IP" = "$IP1" ]; then
    REMOTE_IP=$IP2
else
    REMOTE_IP=$IP1
fi

echo "Current IP: $CURRENT_UVS_IP"
echo "Remote IP: $REMOTE_IP"

DEV_NAME="bonding_dev_0"
EID="eid0"
eid_address=0

get_dev_eid() {
  eid_address=$(urma_admin show | awk -v ubep_dev="$DEV_NAME" -v eid="$EID" '
      $2 == ubep_dev && $4 == eid {
          # 提取第五个字段（eid 地址）
          print $5
          exit
      }
  ')
  # 判断是否找到地址
  if [ -z "$eid_address" ]; then
      eid_address=0
      echo "not found valid eid"
  else
      echo "eid is: $eid_address"
  fi
}

# 优化后的配置文件更新函数
load_conf() {
    local config_path="${MATRIX_ENGINE_INSTALL_PATH}/conf/ubsmd.conf"
    # 检查配置文件是否存在
    if [ ! -f "$config_path" ]; then
        echo "Error: Configuration file not found at $config_path"
        exit 1
    fi
    get_dev_eid
    # 更新配置项
    sed -i "s/ubsm.server.rpc.local.ipseg *=.*$/ubsm.server.rpc.local.ipseg = $CURRENT_UVS_IP:7201/g" "$config_path"
    sed -i "s/ubsm.server.rpc.remote.ipseg *=.*$/ubsm.server.rpc.remote.ipseg = $REMOTE_IP:7201/g" "$config_path"

    sed -i "s/ubsm.lock.dev.name *=.*$/ubsm.lock.dev.name = $DEV_NAME/g" "$config_path"
    sed -i "s/ubsm.lock.dev.eid *=.*$/ubsm.lock.dev.eid = $eid_address/g" "$config_path"
    sed -i "s/ubsm.server.tls.enable *=.*$/ubsm.server.tls.enable = off/g" "$config_path"
  
}

check_status() {
    local delay=20
    local attempt=1
    local node_num=0
    while true; do
        echo "第 $attempt 次检查服务状态..."
        # 执行 rack-cli 命令并捕获输出
        local output=$(sudo -u scbus /usr/local/softbus/ctrlbus-cli/bin/rack-cli check memory)
        echo "$output"
        # 使用 awk 过滤并检查状态
        if echo "$output" | awk '
            BEGIN {
                ok = 1  # 默认状态为 ok
            }
            # 跳过标题行（第1行）
            NR == 1 { next }
            # 跳过status （第2行）
            NR == 2 { next }
            # 跳过分隔线（第3行）
            NR == 3 { next }
            # 跳过空行（第5行）
            NR == 5 { next }
            NR == 7 { next }
            # 检查每行的字段，确保 status 字段是 ok
            {
                if ($2 == "ok") {
                node_num++
                }
                print "处理行:", NR, "字段1:", $1, "字段2:", $2
            }
            END {
                if (node_num >= 2) {
                    exit 0  # 所有节点状态为 ok
                } else {
                    exit 1  # 存在非 ok 状态
                }
            }
        '; then
            echo "服务状态检查成功，继续执行后续脚本。"
            return 0  # 成功，退出函数，继续执行后面的代码
        fi
        echo "检查失败，$delay 秒后重试 ..."
        sleep $delay

        attempt=$((attempt + 1))
    done
    # 所有重试都失败
    echo "错误：已连续 $max_retries 次检查失败，放弃重试，退出脚本。"
    exit 1
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

pre_install_env(){
    local UBS_CONF_PATH=/usr/local/softbus/ctrlbus/conf/rack_uds_user_verify.conf
    systemctl stop scbus-daemon || true
    exec_cmd rpm -ivh $PARENT_DIR/$UBSE_NAME --force --nodeps
    exec_cmd rpm -ivh $PARENT_DIR/ubse-devel-1.0.0-1.aarch64.rpm --force --nodeps
    exec_cmd rpm -ivh $PARENT_DIR/ubse-cli-1.0.0-1.aarch64.rpm --force --nodeps
    exec_cmd grep -q "ubsmd" $UBS_CONF_PATH || sed -i '/^sdk\.user\.whitelist/s/$/,ubsmd/' $UBS_CONF_PATH

    pkill -9 matrix_diagnose || true
    pkill -9 matrix_mem_test || true
    rm -rf /tmp/*.lock
    exec_cmd rm -rf $MATRIX_LOG_PATH
    rpm -evh ubs_mem || true
    exec_cmd rpm -ivh $PARENT_DIR/$MATRIX_MEM_NAME --force --nodeps
    load_conf

    exec_cmd systemctl start scbus-daemon
    exec_cmd export LD_LIBRARY_PATH="$NEW_LD_PATH"
    sleep 40
}

# 检查目录是否存在
if [ ! -d "$UB_REMOTE_PATH" ]; then
    # 如果目录不存在，则创建它
    echo "目录 $UB_REMOTE_PATH不存在，正在创建..."
    mkdir -p "$UB_REMOTE_PATH"
    # 检查 mkdir 命令是否成功
    if [ $? -eq 0 ]; then
        echo "目录 $UB_REMOTE_PATH创建成功。"
    else
        echo "错误：创建目录 $UB_REMOTE_PATH失败！"
        exit 1
    fi
fi

pre_install_env
check_status

exit 0
