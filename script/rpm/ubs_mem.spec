# Restore old style debuginfo creation for rpm >= 4.14.
%undefine _debugsource_packages
%undefine _debuginfo_subpackages

# -*- rpm-spec -*-
%define __strip /bin/true
Summary:        Matrix Core Memory Package
Name:           ubs_mem
Version:        1.0.0
Release:        1.oe2203sp13
License:        MIT
Group:          System Environment/Daemons
Vendor:         Huawei Technologies Co., Ltd.
Prefix:         /usr/local/ubs_mem
Source:         ubs_mem-tmp.tar.gz

%define _rpmfilename %{Name}-memfabric-%{Version}-%{Release}.aarch64.rpm
%define _unpackaged_files_terminate_build 0
%description
This is matrix-core-memory daemon.

%prep
mkdir -p %_sourcedir
rm -rf %_sourcedir/*
rm -rf %_rpmdir/*
rm -rf %_builddir/*
cp -f ~/rpmbuild/matrix-core-memory/daemon/ubs_mem-tmp.tar.gz %_sourcedir/

%build

%install
mkdir -p %{buildroot}/usr/local/ubs_mem/lib
mkdir -p %{buildroot}/usr/local/ubs_mem/bin
mkdir -p %{buildroot}/usr/local/ubs_mem/script
mkdir -p %{buildroot}/usr/local/ubs_mem/config

tar -xf %{_sourcedir}/ubs_mem-tmp.tar.gz -C %{buildroot}/usr/local/ubs_mem ./lib
tar -xf %{_sourcedir}/ubs_mem-tmp.tar.gz -C %{buildroot}/usr/local/ubs_mem ./include
tar -xf %{_sourcedir}/ubs_mem-tmp.tar.gz -C %{buildroot}/usr/local/ubs_mem ./bin
tar -xf %{_sourcedir}/ubs_mem-tmp.tar.gz -C %{buildroot}/usr/local/ubs_mem ./config
tar -xf %{_sourcedir}/ubs_mem-tmp.tar.gz -C %{buildroot}/usr/local/ubs_mem ./script
tar -xf %{_sourcedir}/ubs_mem-tmp.tar.gz -C %{buildroot}/usr/local/ubs_mem ./VERSION

install -Dm644 %{buildroot}/usr/local/ubs_mem/script/ubsmd.service %{buildroot}/usr/lib/systemd/system/ubsmd.service

%clean
rm -rf %{buildroot}

%pre
create_user_and_group() {
    if ! getent group ubsmd > /dev/null; then
        groupadd --system ubsmd
    fi

    if ! getent passwd ubsmd > /dev/null; then
        useradd --system -g ubsmd --no-create-home --shell /sbin/nologin ubsmd
    fi

    usermod -a -G ubse ubsmd
}
stop_old_service() {
    if [ -e /usr/lib/systemd/system/ubsmd.service ]; then
        systemctl stop ubsmd.service || true
        systemctl disable ubsmd.service || true
        rm -f /usr/lib/systemd/system/ubsmd.service
    fi
    rm -f /tmp/matrix_mem_daemon.lock
}

create_user_and_group
stop_old_service

%post
create_log_directory() {
    mkdir -p /var/log/ubsm/
    chown -R ubsmd:ubsmd /var/log/ubsm
}
enable_service() {
    systemctl daemon-reload > /dev/null 2>&1 || :
    systemctl enable ubsmd.service > /dev/null 2>&1 || :
}

create_log_directory
enable_service

%preun
stop_service() {
    systemctl stop ubsmd.service > /dev/null 2>&1 || :
    systemctl disable ubsmd.service > /dev/null 2>&1 || :
}

stop_service

%postun
if [ $1 -ne 0 ]; then # 0 means remove, 1 means update
    exit 0
fi
delete_semaphore() {
    echo "Checking and deleting semaphores..."
    while read -r line; do
        semid=$(echo "$line" | awk '{print $1}')
        owner=$(echo "$line" | awk '{print $2}')
        if [ "$owner" = "ubsmd" ]; then
            echo "Deleting semaphore $semid..."
            if ! ipcrm -s $semid; then
                echo "Failed to delete semaphore $semid"
            else
                echo "Deleted semaphore $semid"
            fi
        fi
    done < <(ipcs -s | awk '/^[0-9]/ {print $2, $3, $4}')
    echo "delete ubsmd semaphores finished"
}
remove_files() {
    systemctl daemon-reload > /dev/null 2>&1 || :
    rm -f /usr/lib/systemd/system/ubsmd.service
    rm -rf /usr/local/ubs_mem
    rm -f /dev/shm/ubsm_records
    rm -rf /run/matrix/
}

remove_files
delete_semaphore

%files
%defattr(550,ubsmd,ubsmd,550)
%dir %attr(750,ubsmd,ubsmd) /usr/local/ubs_mem

%dir %attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/bin
%attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/bin/ubsmd

%dir %attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/lib
%attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/lib/libcrypto.so
%attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/lib/libsecurec.so
%attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/lib/libssl.so
%attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/lib/libubsm_sdk.so
%attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/lib/libubsmd.so

%dir %attr(750,ubsmd,ubsmd) /usr/local/ubs_mem/config
%attr(640,ubsmd,ubsmd) /usr/local/ubs_mem/config/ubsmd.conf

%attr(440,ubsmd,ubsmd) /usr/local/ubs_mem/VERSION

%dir %attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/include
%attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/include/ubs_mem.h
%attr(550,ubsmd,ubsmd) /usr/local/ubs_mem/include/ubs_mem_def.h

%attr(644,root,root) /usr/lib/systemd/system/ubsmd.service
%changelog