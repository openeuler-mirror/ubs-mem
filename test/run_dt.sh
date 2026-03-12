#!/bin/bash

run_encrypt_tool() {
  cd $BUILD_PATH
  local output_file="$BUILD_PATH/keypass.txt"

  mkdir -p $BUILD_PATH/../config

  cat << EOF > $BUILD_PATH/../config/crypto_tool_config.json
{
    "algorithm": "AES256_GCM",
    "keyManagerType": "kmc",
    "thirdKeyManager": {
        "keyManagerPath": "",
        "keyManagerAddr": ""
    }
}
EOF
  result=$(echo "test123")
  echo "$result" | tr -d '\n'> "$output_file"
  echo "  Encrypted string saved to: $output_file"
}

compile_and_run() {
  set -e
  DEBUG_FUZZ=OFF
  cmake -DCMAKE_BUILD_TYPE=Debug -DASAN_BUILD=ON -DDEBUG_UT=ON -DDEBUG_FUZZ=${DEBUG_FUZZ} -S $SRC_PATH -B $BUILD_PATH
  echo "end cmake."
  cd $BUILD_PATH
  pwd;
  N_CPUS=$(grep processor /proc/cpuinfo | wc -l)
  echo "$N_CPUS processors detected."
  make -j $N_CPUS install;
  echo "end make install."
  cp $BUILD_PATH/output/bin/* $BUILD_PATH
  cp $BUILD_PATH/output/lib/* $BUILD_PATH
  cp $BUILD_PATH/3rdparty/openssl/output/lib/libcrypto.so $BUILD_PATH
  cp $BUILD_PATH/3rdparty/openssl/output/lib/libssl.so $BUILD_PATH
  run_encrypt_tool
  ASAN_PATH=$(find /usr/lib64/ -name *asan* |head -n 1)
  LD_PRELOAD=$ASAN_PATH:$LD_PRELOAD LD_LIBRARY_PATH=$BUILD_PATH:$LD_LIBRARY_PATH HSECEASY_PATH=$BUILD_PATH ${BUILD_PATH}/mxmd_ut --gtest_break_on_failure --gtest_output=xml:gcover_report/test_detail.xml
  if [ ${DEBUG_FUZZ} = ON ]; then
    LD_PRELOAD=$ASAN_PATH:$LD_PRELOAD LD_LIBRARY_PATH=$BUILD_PATH:$LD_LIBRARY_PATH HSECEASY_PATH=$BUILD_PATH ${BUILD_PATH}/mxmd_fuzz --gtest_output=xml:gcover_report/test_fuzz_detail.xml
  fi
}

CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)
BUILD_PATH="${CURRENT_PATH}/build"
SRC_PATH="${CURRENT_PATH}/../"
COVERAGE_PATH="${BUILD_PATH}/coverage"

rm -rf "$COVERAGE_PATH"

set -e
sh ${CURRENT_PATH}/cert.sh
start_compile=$(date +%s%3N)
compile_and_run
end_compile=$(date +%s%3N)

# sh coverage.sh ${SRC_PATH} ${TEST_PATH} 统计覆盖率
start_coverage=$(date +%s%3N)
sh  ${CURRENT_PATH}/coverage.sh ${SRC_PATH} ${CURRENT_PATH}
end_coverage=$(date +%s%3N)

echo "The time consumed by each step is as follows:"
echo "update_deps: $(((end_update_deps - start_update_deps)/1000)).$(((end_update_deps - start_update_deps)%1000))s"
echo "compile_and_run: $(((end_compile - start_compile)/1000)).$(((end_compile - start_compile)%1000))s"
echo "coverage: $(((end_coverage - start_coverage)/1000)).$(((end_coverage - start_coverage)%1000))s"