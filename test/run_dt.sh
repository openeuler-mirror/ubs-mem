#!/bin/bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 [--skip-run-tests] [--coverage] [--fast] [--jobs <count>]

  --skip-run-tests  Build unit tests without running them.
  --coverage        Generate the lcov HTML report after tests (disabled by default).
  --fast            Stop at the first GoogleTest failure.
  --jobs <count>    Set build parallelism (default: 60% of available CPUs).

BUILD_JOBS can also set the build parallelism. --jobs takes precedence.
EOF
}

apply_mockcpp_patch() {
    local mockcpp_src_dir="$1"
    local patch_file="$2"

    if [ -z "$mockcpp_src_dir" ] || [ -z "$patch_file" ]; then
        echo "[ERROR] Usage: apply_mockcpp_patch <mockcpp_source_dir> <patch_file>"
        return 1
    fi
    if [ ! -e "$patch_file" ]; then
        echo "[ERROR] Patch file not found: $patch_file"
        return 1
    fi

    echo "[STATUS] Checking mockcpp patch: $patch_file"
    while read -r file; do
        if [ -e "$mockcpp_src_dir/$file" ]; then
            sed -i 's/\r$//' "$mockcpp_src_dir/$file"
        fi
    done < <(sed -n 's|^--- a/||p' "$patch_file")

    if patch --batch --silent --forward --dry-run --reverse -p1 -d "$mockcpp_src_dir" < "$patch_file"; then
        echo "[STATUS] ARM64 patch already applied, skipping."
        return 0
    fi

    # A dry run prevents patch from leaving the submodule partially modified.
    if ! patch --batch --silent --forward --dry-run -p1 -d "$mockcpp_src_dir" < "$patch_file"; then
        echo "[ERROR] ARM64 patch does not apply cleanly."
        return 1
    fi

    echo "[INFO] Applying patch..."
    patch --batch --forward --no-backup-if-mismatch -p1 -d "$mockcpp_src_dir" < "$patch_file"
    echo "[STATUS] Patch applied successfully."
}

update_3rdparty() {
    git -C "$SRC_PATH" submodule update --init --recursive --depth 1
    apply_mockcpp_patch "$CURRENT_PATH/3rdparty/mockcpp" "$CURRENT_PATH/3rdparty/mockcpp_support_arm64.patch"
}

run_encrypt_tool() {
    cd "$BUILD_PATH"
    local output_file="$BUILD_PATH/keypass.txt"

    mkdir -p "$BUILD_PATH/../config"
    cat <<EOF > "$BUILD_PATH/../config/crypto_tool_config.json"
{
    "algorithm": "AES256_GCM",
    "keyManagerType": "kmc",
    "thirdKeyManager": {
        "keyManagerPath": "",
        "keyManagerAddr": ""
    }
}
EOF
    echo "test123" | tr -d '\n' > "$output_file"
    echo "  Encrypted string saved to: $output_file"
}

compile_and_run() {
    local debug_fuzz=OFF
    cmake -DCMAKE_BUILD_TYPE=Debug -DASAN_BUILD=ON -DBUILD_TEST=ON -DDEBUG_UT=ON \
        -DDEBUG_FUZZ=${debug_fuzz} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S "$SRC_PATH" -B "$BUILD_PATH"
    echo "end cmake."
    cd "$BUILD_PATH"

    local available_cpus
    available_cpus=$(nproc)
    local default_jobs=$((available_cpus * 6 / 10))
    if [ "$default_jobs" -lt 1 ]; then
        default_jobs=1
    fi
    local build_jobs=${REQUESTED_JOBS:-${BUILD_JOBS:-$default_jobs}}
    if ! [[ "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then
        echo "[ERROR] Build job count must be a positive integer: $build_jobs"
        return 1
    fi
    echo "$available_cpus processors available; building with $build_jobs jobs."
    make -j "$build_jobs" install
    echo "end make install."

    if [ "$SKIP_RUN_TESTS" = true ]; then
        return
    fi

    cp "$BUILD_PATH"/output/bin/* "$BUILD_PATH"
    cp "$BUILD_PATH"/output/lib/* "$BUILD_PATH"
    run_encrypt_tool
    mkdir -p "$BUILD_PATH/gcover_report"

    local gtest_args=(--gtest_output=xml:gcover_report/test_detail.xml)
    if [ "$FAST_MODE" = true ]; then
        gtest_args+=(--gtest_break_on_failure)
    fi

    # LeakSanitizer requires ptrace, which is blocked by Docker's default seccomp profile.
    local asan_options=${ASAN_OPTIONS:-detect_leaks=0}
    ASAN_OPTIONS="$asan_options" LD_LIBRARY_PATH="$BUILD_PATH" HSECEASY_PATH="$BUILD_PATH" \
        "$BUILD_PATH/mxmd_ut" "${gtest_args[@]}"
    if [ "$debug_fuzz" = ON ]; then
        ASAN_OPTIONS="$asan_options" LD_LIBRARY_PATH="$BUILD_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            HSECEASY_PATH="$BUILD_PATH" \
            "$BUILD_PATH/mxmd_fuzz" --gtest_output=xml:gcover_report/test_fuzz_detail.xml
    fi
}

SKIP_RUN_TESTS=false
ENABLE_COVERAGE=false
FAST_MODE=false
REQUESTED_JOBS=
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-run-tests)
            SKIP_RUN_TESTS=true
            shift
            ;;
        --coverage)
            ENABLE_COVERAGE=true
            shift
            ;;
        --fast)
            FAST_MODE=true
            shift
            ;;
        --jobs)
            if [ $# -lt 2 ]; then
                echo "[ERROR] --jobs requires a value."
                usage
                exit 1
            fi
            REQUESTED_JOBS=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "[ERROR] Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

if [ "$SKIP_RUN_TESTS" = true ] && [ "$ENABLE_COVERAGE" = true ]; then
    echo "[ERROR] --coverage cannot be used with --skip-run-tests."
    exit 1
fi

CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)
BUILD_PATH="${CURRENT_PATH}/build"
SRC_PATH="${CURRENT_PATH}/../"
COVERAGE_PATH="${BUILD_PATH}/coverage"

start_update_deps=$(date +%s%3N)
update_3rdparty
end_update_deps=$(date +%s%3N)
if [ "$ENABLE_COVERAGE" = true ]; then
    rm -rf "$COVERAGE_PATH"
fi

sh "${CURRENT_PATH}/cert.sh"
start_compile=$(date +%s%3N)
compile_and_run
end_compile=$(date +%s%3N)

if [ "$ENABLE_COVERAGE" = true ]; then
    start_coverage=$(date +%s%3N)
    sh "${CURRENT_PATH}/coverage.sh" "$SRC_PATH" "$CURRENT_PATH"
    end_coverage=$(date +%s%3N)
fi

echo "The time consumed by each step is as follows:"
echo "update_deps: $(((end_update_deps - start_update_deps)/1000)).$(((end_update_deps - start_update_deps)%1000))s"
echo "compile_and_run: $(((end_compile - start_compile)/1000)).$(((end_compile - start_compile)%1000))s"
if [ "$ENABLE_COVERAGE" = true ]; then
    echo "coverage: $(((end_coverage - start_coverage)/1000)).$(((end_coverage - start_coverage)%1000))s"
fi
