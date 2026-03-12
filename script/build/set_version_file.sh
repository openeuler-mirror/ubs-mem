#!/bin/bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
# build script.
# 获取脚本所在目录的绝对路径
script_dir="$(cd "$(dirname "$0")" && pwd)"

properties_file="$script_dir/version.properties"
# 检查环境变量是否已设置
required_vars=("CCSUITE_PRODUCTNAME" "CCSUITE_VERSION" "CCSUITE_PATCHNUM" "CCSUITE_OSTYPE"
               "CCSUITE_OSARCH" "CCSUITE_BUILDNUM" "CCSUITE_BUILDTIME")
for var in "${required_vars[@]}"; do
  if [[ "$var" == "CCSUITE_PATCHNUM" ]]; then
    if [[ -z "${!var}" ]]; then
      sed -i "s/@$var@//g" "$properties_file"
    else
      sed -i "s/@$var@/${!var}/g" "$properties_file"
    fi
  else
    if [[ -z "${!var}" ]]; then
      echo "missing environment: $var"
      exit 1
    fi
    sed -i "s/@$var@/${!var}/g" "$properties_file"
  fi
done

echo "set version file done."