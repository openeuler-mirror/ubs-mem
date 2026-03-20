#!/bin/bash

usage() {
    echo "Usage: $0 [ -h | --help ] [ -t | --type <build_type> ] [--ut=UT] [--cov=COV] [ -d | --docker ] [ -n | --ninja ] \
     [ -b | --builddir <build_path> ] [ -c | --component ] [ -f | --flags <cmake_flags> ] [ -p | --packaging]"
    echo "build_type: [debug, release, asan, tsan, clean]"
	echo "ut: unit test, default don't exic ut when not specified, <on|off> [default: "off"]"
    echo "cov: Instrument code coverage, default is no instrumentation when not specified, <on|off> [default: "off"]"
    echo "docker: enable docker build"
    echo "packaging: generate product distribution package"
    echo "builddir: specify build directory instead of using the default aka. Build"
    echo "ninja: use ninja instead of make as cmake generator"
    echo "cmake_flags: customized flags passed to cmake (these arguments must appear after all other arguments)"
    echo "component: generate componet package"
    echo
    echo "Examples:"
    echo " 1 ./build.sh -t debug"
    echo " 2 ./build.sh -t release"
    echo " 3 ./build.sh -t asan"
    echo
    exit 1;
}

BUILD_DIR=
BUILD_FOLDER=debug
BUILD_TYPE=Debug
BUILD_TOOL=ninja
CMAKE_FLAGS=
PACKAGING=false
PACKAGING_DEB=false
PACK_COMPONENT=false
UBSE_SDK=ON
CMAKE_FLAGS+='-G Ninja '
# Parse the argument params
while true; do
    case "$1" in
        -b | --builddir )
            if [[ ! -d $2 ]]; then
                echo $2 does not exist!
                exit 1
            fi
            BUILD_DIR=$(realpath $2)
            shift 2
            ;;
        -t | --type )
            type=$2
            type=${type,,}
            [[ $type != "debug" && $type != "release" && $type != "asan" && $type != "tsan" && $type != "clean" ]] && echo "Invalid build type $2" && usage
            if [[ $type == 'debug' ]]; then
              BUILD_TYPE=Debug
              BUILD_FOLDER=debug
            elif [[ $type == 'release' ]]; then
              BUILD_TYPE=Release
              BUILD_FOLDER=release
            elif [[ $type == 'asan' ]]; then
              BUILD_TYPE=Debug
              BUILD_FOLDER=asan
              CMAKE_FLAGS+='-DASAN_BUILD=ON '
            elif [[ $type == 'tsan' ]]; then
              BUILD_TYPE=Debug
              BUILD_FOLDER=tsan
              CMAKE_FLAGS+='-DSAN_BUILD=ON '
            elif [[ $type == 'clean' ]]; then
              BUILD_TYPE=CLEAN
            fi
            shift 2
            ;;
        --ut )
            USING_UT=$(echo "$2"|tr a-z A-Z|tr -d "'")
            CMAKE_FLAGS+="-DDEBUG_UT=${USING_UT} "
            shift 2
            ;;
        --ubse )
            UBSE_SDK=ON
            shift ;;
        --cov )
            USING_COVERAGE=$(echo "$2"|tr a-z A-Z|tr -d "'")
            CMAKE_FLAGS+="-DUSING_COVERAGE=${USING_COVERAGE} "
            shift 2
            ;;
        -d | --docker )
            CMAKE_FLAGS+='-DDOCKER=ON '
            shift ;;
        -p | --packaging )
            PACKAGING=true
            shift ;;
        -pdeb )
            PACKAGING_DEB=true
            shift ;;
        -c | --component )
            PACK_COMPONENT=true
            shift ;;
        -n | --ninja )
            CMAKE_FLAGS+='-G Ninja '
            BUILD_TOOL=ninja
            shift ;;
        -f | --flags )
            while [[ $2 ]]; do
                CMAKE_FLAGS+="$2 "
                shift
            done
            ;;
        -l | --pipeline )
            CMAKE_FLAGS+='-DAUTO_CI_TEST=ON '
            shift ;;
        -h | --help )
            usage
            exit 0
            ;;
        * )
            break;;
    esac
done

# Retrieve project top directory
PROJ_DIR="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${PROJ_DIR}")"

cd ${PROJ_DIR}

if [ "$BUILD_TYPE" == "CLEAN" ]; then
    rm -rf $PROJ_DIR/build
    exit 0
fi

if [ -z "$BUILD_DIR" ]; then
  BUILD_DIR=$PROJ_DIR/build/$BUILD_FOLDER
fi
if [ -z "$CI_BUILD" ] && [ ! -d "$BUILD_DIR/output" ];then
    echo "update submodules ... "
    git submodule sync
    git submodule update --init --recursive
fi

# Setup the build directory
if [[ ! -d "$BUILD_DIR" ]]
then
  mkdir -p $BUILD_DIR
fi

# Verify the build directory is in place and enter it
cd $BUILD_DIR || {
  echo "Fatal! Cannot enter $BUILD_DIR."
  exit 1;
}

# Check number of physical processors for parallel make
N_CPUS=$(grep processor /proc/cpuinfo | wc -l)
if [ -n "$CI_BUILD" ];then
    N_CPUS=16
fi
echo "$N_CPUS processors detected."

echo UBSE_SDK is "$UBSE_SDK"
if [ "$UBSE_SDK" == "ON" ]; then
	  CMAKE_FLAGS+='-DUBSE_SDK=ON'
else
    CMAKE_FLAGS+='-DUBSE_SDK=OFF'
fi

# Now do the build job
CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS $PROJ_DIR"
BUILD_CMD="$BUILD_TOOL -j $((N_CPUS-2)) install"
PACKAGE_CMD="cpack -G TGZ"

echo $CMAKE_CMD
$CMAKE_CMD || {
    echo "Failed to configure ubsm build!"
    exit 1
}
echo
echo "Done configuring ubsm build"
echo
echo $BUILD_CMD
$BUILD_CMD || {
    find . -name "*err.log" 2>/dev/null | while read ERROR_FILE_NAME
    do
        if [ -s "$ERROR_FILE_NAME" ]; then
          echo "###LOG### $ERROR_FILE_NAME"
        fi
    done
    echo "Failed to build ubsm"
    exit 1
}
echo
echo "Done building ubsm"
echo

cp ${PROJ_DIR}/script/install_and_start_ubsmd.sh output/script
cd $BUILD_DIR/output/
# 版本信息
echo -e "B_Version:$B_Version\nCommitId:$commitId" > VERSION

if [ "$PACKAGING" == "true" ]; then
      RPM_SRC_PATH=~/rpmbuild/matrix-core-memory/daemon/
      RPM_SPEC_FILE=$PROJ_DIR/script/rpm/ubs_mem.spec

      echo "start to package matrix memory..."
      cd $BUILD_DIR/output/
      tar -czf ubs_mem-tmp.tar.gz ./bin/ ./config/ ./lib/ ./script/ ./include ./VERSION

      cd $BUILD_DIR/output_test/
      mkdir script
      mkdir lib
      cp ../output/script/ubs_mem_smoke.sh ./script
      cp ../output/script/ubs_mem_start.sh ./script
      cp ../output/lib/*.so.debug.* ./lib
      cp ../output/lib/libdecrypt.so ./lib
      cp ../output/bin/*.debug.* ./bin
      cp ../output/script/install_and_start_ubsmd.sh ./script
      zip -r ubs_mem_test.zip ./bin/ ./script/ ./lib/
      echo "package matrix memory end"
      \mv ubs_mem_test.zip ../output/

      cd $BUILD_DIR/output/
      mkdir -p $RPM_SRC_PATH
      rm $RPM_SRC_PATH/ubs_mem-tmp.tar.gz -f
      cp ubs_mem-tmp.tar.gz $RPM_SRC_PATH
      echo "Start rpmbuild."
      RPM_BUILD_OUTPUT=$(QA_SKIP_RPATHS=1 rpmbuild -bb --clean ${RPM_SPEC_FILE})
      echo "$RPM_BUILD_OUTPUT" > /dev/null 2>&1
      cp -f ~/rpmbuild/RPMS/ubs-mem-memfabric-*.rpm $BUILD_DIR/output || {
          echo "Failed to rpm ubs_mem!"
          exit 1;
      }
      echo "End rpmbuild."
fi

if [ "$PACKAGING_DEB" == "true" ]; then
    echo "Starting to package ubs_mem as .deb..."

    cd $BUILD_DIR/output/

    # ========== 1. 准备文件 ==========
    # 打包主程序文件（可用于 deb 内部归档或调试）
    tar -czf ubs_mem-tmp.tar.gz ./bin/ ./config/ ./lib/ ./script/ ./include ./VERSION


    # ========== 2. 创建调试 zip 包（可选）==========
    cd $BUILD_DIR/output_test/
    mkdir -p script lib bin

    cp ../output/script/ubs_mem_smoke.sh ./script/
    cp ../output/script/ubs_mem_start.sh ./script/
    cp ../output/lib/*.so.debug.* ./lib/
    cp ../output/bin/*.debug.* ./bin/
    cp ../output/script/install_and_start_ubsmd.sh ./script/

    zip -r ubs_mem_test.zip ./bin/ ./script/ ./lib/
    mv ubs_mem_test.zip ../output/
    echo "Debug zip package created."


    # ========== 3. 开始构建 DEB 包 ==========
    cd $BUILD_DIR/output/
    : "${B_Version:=25.3.T1.B099}"
    # 配置参数
    PACKAGE_NAME="ubsmem"                        # 包名（小写，无下划线）
    VERSION="$B_Version"                          # 版本号
    ARCH="arm64"                                  # 架构：amd64, arm64, i386 等
    DEB_ROOT=$BUILD_DIR/deb-root                  # 临时根目录
    OUTPUT_DEB="${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"

    # 清理旧目录
    rm -rf $DEB_ROOT
    mkdir -p $DEB_ROOT/usr/local/$PACKAGE_NAME
    mkdir -p $DEB_ROOT/DEBIAN

    # 复制项目文件到 deb 根目录
    cp -r ./bin ./config ./lib ./script ./include ./VERSION $DEB_ROOT/usr/local/$PACKAGE_NAME/

    # ========== 4. 创建 DEBIAN/control 文件 ==========
    cat > $DEB_ROOT/DEBIAN/control << EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Your Name <your.email@example.com>
Depends: libc6, bash, coreutils
Description: TestPackage daemon service
 This package provides the testPackage background service.
 It includes binaries, configs, and startup scripts.
EOF

    # --- 创建 DEBIAN/prerm 脚本 ---
    cat > $DEB_ROOT/DEBIAN/prerm << 'EOF'
#!/bin/sh
set -e
# 删除残留文件 /dev/shm/ubsm_records
if [ -f "/dev/shm/ubsm_records" ]; then
    rm -f /dev/shm/ubsm_records
    echo "Removed /dev/shm/ubsm_records"
fi
exit 0
EOF
    chmod 755 $DEB_ROOT/DEBIAN/prerm

    # ========== 5. 设置权限 ==========
    chmod 500 $DEB_ROOT/DEBIAN
    find $DEB_ROOT -type d -exec chmod 755 {} \;
    # 推荐设置所有者为 root（避免 dpkg 警告）
    chown -R root:root $DEB_ROOT
    chmod -R 500 $DEB_ROOT/usr/local/$PACKAGE_NAME


    # ========== 6. 使用 dpkg-deb 打包 ==========
    echo "Building DEB package: $OUTPUT_DEB"
    dpkg-deb --build $DEB_ROOT $OUTPUT_DEB

    if [ $? -ne 0 ]; then
        echo "Failed to build DEB package!"
        exit 1
    fi

    echo "DEB package built successfully: $BUILD_DIR/output/$OUTPUT_DEB"
fi

echo ${PROJ_DIR}

echo Success
