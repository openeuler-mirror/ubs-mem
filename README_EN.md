# ubs-mem

## Overview

Unified Bus Service Core Memory (UBS Memory) provides advanced memory services on supernodes based on UB hardware capabilities, enabling memory borrowing, sharing, and caching across supernodes.

## Environment Requirements

OS: openEuler 24.03 LTS SP3 or later is recommended.

Architecture: the project does not restrict the build architecture and can be built with the x86_64 or aarch64
toolchain supplied by the target operating system. On aarch64, the unit-test script automatically applies the
mockcpp instruction-jump and cache-flush patch.

## Software Requirements

**Build tools:**

- `rpm-build`: used to generate the RPM installation package
- `git`: used to obtain the source and generate the RPM source archive
- `cmake` (≥ 3.13): cross-platform build system
- `ninja-build`: high-speed build tool to replace Make

**Compilation environment:**

- `gcc` (≥ 10.3.1): C language compiler
- `gcc-c++` (≥ 10.3.1): C++ language compiler

**Dependency libraries:**

- `numactl-devel`: non-uniform memory access (NUMA) support library
- `systemd-devel`: systemd service management support library
- `libboundscheck`: security function library, providing `/usr/include/securec.h`, including secure C APIs such as `memcpy_s`.
- `ubs-comm-devel`: UBS communication development package, providing `/usr/include/hcom/hcom_service.h` and HCOM link libraries.

On openEuler 24.03 LTS SP3 or later, install the build dependencies with:

```shell
dnf install -y rpm-build git make cmake gcc gcc-c++ ninja-build \
    numactl-devel systemd-devel libboundscheck ubs-comm-devel
```

`ubs-comm-devel` installs the runtime package `ubs-comm-lib` as a dependency. This project uses `hcom_service.h`; its
libraries are installed in `/usr/lib64` by default. If these packages are unavailable from the configured repositories,
obtain the source and follow the build
instructions in the [UBS Comm](https://gitcode.com/openeuler/ubs-comm) project.

## Get Code

```shell
git clone https://gitcode.com/openeuler/ubs-mem.git

cd ubs-mem
```

## Build the Project

The repository provides the unified `build.sh` script. Use `-t` to select a direct build type such as debug or
release:

```shell
sh build.sh -t release
```

By default, the build uses 50% of the available CPUs, with at least one job. Set the parallelism with `--jobs`
(or `-j`) or the `BUILD_JOBS` environment variable. The command-line option takes precedence:

```shell
sh build.sh -t release --jobs 8
BUILD_JOBS=8 sh build.sh -t release
```

Direct build artifacts are written to `build/release/output`. Generate RPM packages with:

```shell
sh build.sh -p
```

Packaging performs a separate RelWithDebInfo build through `ubs-mem.spec`, so `-t` is ignored when used together
with `-p`. RPM packages are written to `build/rpm`.

## Project Structure

```text
.
├── build   // Stores script files used in the project.
├── doc       // Stores project documents, such as the code architecture design.
├── src       // Stores the source code for implementing project functions. Only this directory is involved in package building.
├── test      // Stores the UT and DTFuzz files of the project.
└── build.sh  // Unified build entrance.
```

## Developer Testing

Install `git`, `patch`, `libasan`, `openssl`, and `openssl-devel` before running unit tests:

```shell
dnf install -y git patch libasan openssl openssl-devel
```

On its first run, `sh run_dt.sh` downloads the `googletest` and `mockcpp` Git submodules from the repositories
configured in `.gitmodules`. Ensure those repositories are reachable.

```shell
cd test

# Build and run all unit tests. Coverage is disabled by default.
sh run_dt.sh

# Stop at the first failure.
sh run_dt.sh --fast

# Build unit tests without running them.
sh run_dt.sh --skip-run-tests

# Set build parallelism. The default is 60% of the available CPUs.
sh run_dt.sh --jobs 8
```

Coverage is disabled by default. If `lcov` and `genhtml` are installed, run `sh run_dt.sh --coverage`. The detailed
report is written to `test/build/gcovr_report/index.html`.

## Instruction

- **Installation and Deployment**
    For details about the installation and deployment, see [Installation and Deployment](docs/zh/installation_deployment.md).
- **API Reference**
    For details about the API, see [API Description](docs/zh/api_description.md).
    The shared-memory and memory-borrowing examples require UB hardware, UBS Engine, and a deployed ubsmd service;
    they are not standalone examples for an environment without the required hardware.
- **Shared-memory example**
    See [example/README.md](example/README.md) for a complete example that creates, maps, reads, writes, and releases
    a 128 MB `UBSM_FLAG_CACHE` shared-memory object.

## License

ubs-mem uses the Mulan V2 license.

## How to Contribute

Read the [contribution guide](CONTRIBUTING.md) to learn how to contribute to the project.
