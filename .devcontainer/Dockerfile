FROM hub.oepkgs.net/openeuler/openeuler:24.03-lts-sp3

LABEL description="ubs-mem development container"

RUN dnf makecache && dnf install -y \
    git \
    patch \
    rpm-build \
    make \
    cmake \
    gcc \
    gcc-c++ \
    ninja-build \
    numactl-devel \
    systemd-devel \
    libboundscheck \
    ubs-comm-devel \
    libasan \
    openssl \
    openssl-devel \
    python3-pip \
    clang-tools-extra \
    && dnf clean all

COPY .devcontainer/requirements.txt /tmp/requirements.txt
RUN pip3 install --no-cache-dir -r /tmp/requirements.txt

WORKDIR /workspaces/ubs-mem
