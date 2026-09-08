# AGENTS.md

This file is the operational guide for AI coding agents working in the
`ubs-mem` repository. Follow it together with `CONTRIBUTING.md` and the
project documentation. Commands below are run from the repository root unless
another directory is shown.

## Project Overview

UBS Memory (`ubs-mem`) provides memory borrowing, sharing, and caching
services on UB supernodes. The repository contains the C/C++ daemon, SDK,
unit tests, examples, deployment files, and RPM/DEB packaging metadata.

- Primary language: C++17, with some C sources.
- Build system: CMake 3.13 or newer, normally driven by `build.sh` and Ninja.
- Supported build toolchains: the `x86_64` or `aarch64` toolchain supplied by
  openEuler. openEuler 24.03 LTS SP3 or later is recommended.
- Runtime dependencies include UBS Engine, UBS Comm, NUMA, systemd, and
  libboundscheck.
- License: Mulan PSL v2. Keep the existing license headers in source files.

## Build Commands

Install the build dependencies before building on openEuler:

```shell
dnf install -y rpm-build git make cmake gcc gcc-c++ ninja-build \
    numactl-devel systemd-devel libboundscheck ubs-comm-devel
```

The supported build entry point is `build.sh`:

```shell
# Release build. The default parallelism is half of the available CPUs.
bash build.sh -t release

# Debug build with explicit parallelism.
bash build.sh -t debug --jobs 8

# Other supported direct build types: relwithdebinfo, asan, tsan.
bash build.sh -t relwithdebinfo

# Generate an RPM or DEB package.
bash build.sh -p
bash build.sh -pdeb

# Remove the root build directory.
bash build.sh -t clean
```

Direct build artifacts are written to `build/<type>/output`. RPM and DEB
packages are written to `build/rpm` and `build/deb`, respectively. Packaging
uses the repository `ubs-mem.spec` and creates a source archive from the
current Git commit, so do not package an uncommitted working tree.

For a configure-only or custom CMake build, use an out-of-source build
directory:

```shell
cmake -S . -B build/manual \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TEST=OFF \
    -DENABLE_PTRACER=OFF
cmake --build build/manual --target install --parallel 8
```

## Test Commands

Initialize the test submodules before the first test run. This operation
requires network access to the repositories listed in `.gitmodules`:

```shell
git submodule update --init --recursive
```

The unit-test script configures an ASan build, builds the tests, generates
test-only certificates, and runs the GoogleTest binary:

```shell
cd test
bash run_dt.sh

# Stop on the first GoogleTest failure.
bash run_dt.sh --fast

# Build the unit tests without running them.
bash run_dt.sh --skip-run-tests

# Set test build parallelism.
bash run_dt.sh --jobs 8

# Generate coverage when lcov and genhtml are installed.
bash run_dt.sh --coverage
```

Coverage output is placed under `test/build/gcov_report`. On `aarch64`, the
test script applies the repository's mockcpp instruction and cache-flush patch
to the test submodule. Do not commit the resulting submodule working-tree
changes.

## Code Style

- Format C and C++ files with the root `.clang-format` using clang-format 18.
- Use C++17 for C++ code and preserve the project's four-space indentation and
  120-column limit.
- Keep include ordering and existing naming conventions consistent with nearby
  code. Do not reformat unrelated files.
- Normal builds enable strict warnings, including `-Werror`; fix warnings
  introduced by a change rather than suppressing them broadly.
- Run the applicable pre-commit checks before submitting a change:

```shell
pre-commit run --all-files
pre-commit run --hook-stage manual run-build
pre-commit run --hook-stage manual run-build-test
```

The manual build hooks are expensive. For documentation-only changes, at
minimum run the regular pre-commit checks and `git diff --check`.

## Dev Environment Tips

- The `.devcontainer/` configuration is based on openEuler 24.03 LTS SP3 and
  installs the compiler, CMake, Ninja, runtime development libraries,
  sanitizers, OpenSSL, clang tools, and pre-commit.
- The dev container's post-create script initializes submodules and performs a
  smoke CMake configure with `BUILD_TEST=OFF` and `ENABLE_PTRACER=OFF`.
- `build.sh` uses Ninja by default. `--jobs` takes precedence over the
  `BUILD_JOBS` environment variable.
- Unit tests need `git`, `patch`, `libasan`, `openssl`, and `openssl-devel` in
  addition to the build dependencies.
- Build and test outputs are ignored by Git. Keep generated files in the
  existing `build/` or `test/build/` directories.
- Integration examples require UB hardware, UBS Engine, and a deployed
  `ubsmd`; they are not standalone host-only tests.

## Architecture

The top-level CMake project builds the implementation in `src/` and adds
tests only when requested. Important paths are:

```text
src/                  Core implementation and public headers.
src/include/          Public and internal interface headers.
src/communication/    IPC and RPC communication.
src/process/          Daemon and process lifecycle code.
src/security/         Certificate and security handling.
src/store/            Record and allocation storage.
src/app_lib/          SDK-facing application libraries.
src/mxm_shm/          Shared-memory functionality.
src/mxm_lease/        Memory lease functionality.
src/under_api/        Lower-level external API adapters.
3rdparty/             In-tree third-party interfaces and dependencies.
test/                 Unit tests, fuzz targets, test tools, and test scripts.
docs/zh/              Installation, API, configuration, design, and security docs.
script/               Build/package scripts, service unit, and default config.
example/              User-facing shared-memory example and its CMake project.
```

The installed daemon is `ubsmd`; the SDK library and public headers are
installed under the prefix selected by the CMake install rules. The RPM spec
packages the daemon, SDK libraries, configuration, headers, and systemd unit.

## Security Guidelines

- Never commit private keys, certificates, tokens, passwords, generated test
  credentials, or host-specific configuration.
- Keep TLS enabled for production communication. TLS paths and credentials
  are supplied by the deployment environment; the repository does not provide
  production certificates or keys.
- Review `docs/zh/security_description.md` before changing authentication,
  IPC/RPC communication, logging, permissions, or cryptographic behavior.
- The installed daemon runs as the restricted `ubsmd` user and group. Do not
  weaken service permissions, installation modes, or systemd hardening without
  an explicit issue and corresponding tests/documentation.
- Preserve the compiler and linker hardening flags in `cmake/building.cmake`.
- Test certificates generated by `test/cert.sh` are for unit tests only and
  must not be reused in deployments.
- Treat changes to package scriptlets, service dependencies, filesystem paths,
  and external command execution as security-sensitive.

## Commit Guidelines

- Start from the current upstream `master` branch and use a focused feature or
  fix branch. Check `git status` before editing and preserve unrelated local
  changes.
- Keep each commit limited to one logical change. Do not commit build output,
  coverage reports, modified third-party submodules, or editor metadata.
- Use a concise imperative subject with the existing project-style scope when
  useful, for example `docs: add AGENTS.md`.
- For behavior changes, link or reference the relevant Issue, add or update
  tests, and describe compatibility or deployment impact in the PR.
- Before committing, run the applicable tests and checks, then inspect the
  complete diff with `git diff --check` and `git status`.
- Do not rewrite published commits or force-push. Do not modify files outside
  the requested scope without explaining why.
