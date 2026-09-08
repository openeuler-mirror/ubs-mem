#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

echo "Initializing ubs-mem development environment in ${PROJECT_DIR}"
git -C "$PROJECT_DIR" submodule update --init --recursive

cmake -S "$PROJECT_DIR" -B /tmp/ubs-mem-build-smoke \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TEST=OFF \
    -DENABLE_PTRACER=OFF
rm -rf /tmp/ubs-mem-build-smoke

cd "$PROJECT_DIR"
pre-commit install
echo "ubs-mem development container is ready."
