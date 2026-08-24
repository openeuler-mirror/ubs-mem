# Restore old style debuginfo creation for rpm >= 4.14.
%undefine _debugsource_packages
%undefine _debuginfo_subpackages

# -*- rpm-spec -*-
%define __strip /bin/true
Summary:        UBS-MEM Package
Name:           ubs-mem
Version:        1.0.0
Release:        1%{?dist}
License:        MulanPSL-2.0
Group:          System Environment/Daemons
# generate tarball: git archive -o ubs-mem-1.0.0.tar.gz --format=tar.gz HEAD
Source:        ubs-mem-%{version}.tar.gz
BuildRequires:  rpm-build, make, cmake, gcc, gcc-c++, ninja-build
BuildRequires:  libboundscheck, ubs-comm-devel, numactl-devel, systemd-devel
Requires:       %{name}-shmem = %{version}-%{release}

%define _unpackaged_files_terminate_build 0

%description
UBS Memory

%package shmem
Summary:        UBS-MEM Shared Memory subpackage
Group:          System Environment/Daemons
Requires:       glibc libgcc libstdc++ libboundscheck ubs-comm-lib openssl-libs
Requires:       ubs-engine
Requires:       ubs-engine-client-libs
Provides:       ubs-mem-kshmem = %{version}-%{release}
Obsoletes:      ubs-mem-kshmem < %{version}-%{release}

%description shmem
This package contains the shared memory components for ubs-mem.

%prep
%autosetup -c -n %{name}-%{version}

%build
export CI_BUILD=ON
bash build.sh -t relwithdebinfo

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir} %{buildroot}%{_libdir} %{buildroot}%{_prefix}/lib/ubs_mem
mkdir -p %{buildroot}%{_includedir} %{buildroot}%{_sysconfdir}/ubs_mem
install -m 550 %{_builddir}/%{name}-%{version}/build/relwithdebinfo/output/lib/libubsm_sdk.so %{buildroot}%{_libdir}/
install -m 550 %{_builddir}/%{name}-%{version}/build/relwithdebinfo/output/lib/libubsmd.so %{buildroot}%{_prefix}/lib/ubs_mem/
install -m 550 %{_builddir}/%{name}-%{version}/build/relwithdebinfo/output/bin/ubsmd %{buildroot}%{_bindir}/
install -m 640 %{_builddir}/%{name}-%{version}/build/relwithdebinfo/output/config/ubsmd.conf %{buildroot}%{_sysconfdir}/ubs_mem/
install -m 640 %{_builddir}/%{name}-%{version}/build/relwithdebinfo/output/include/ubs_mem.h %{buildroot}%{_includedir}/
install -m 640 %{_builddir}/%{name}-%{version}/build/relwithdebinfo/output/include/ubs_mem_def.h %{buildroot}%{_includedir}/
install -Dm 644 %{_builddir}/%{name}-%{version}/build/relwithdebinfo/output/script/ubsmd.service %{buildroot}/usr/lib/systemd/system/ubsmd.service

%clean
rm -rf %{buildroot}

%pre shmem
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

%post shmem
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
/sbin/ldconfig > /dev/null 2>&1 || :

%preun shmem
stop_service() {
    systemctl stop ubsmd.service > /dev/null 2>&1 || :
    systemctl disable ubsmd.service > /dev/null 2>&1 || :
}

stop_service

%postun shmem
/sbin/ldconfig > /dev/null 2>&1 || :
if [ "$1" -ne 0 ]; then # 0 means remove, 1 means update
    exit 0
fi
delete_semaphore() {
    echo "Checking and deleting semaphores..."
    while read -r line; do
        semid=$(echo "$line" | awk '{print $1}')
        owner=$(echo "$line" | awk '{print $2}')
        if [ "$owner" = "ubsmd" ]; then
            echo "Deleting semaphore $semid..."
            if ! ipcrm -s "$semid"; then
                echo "Failed to delete semaphore $semid"
            else
                echo "Deleted semaphore $semid"
            fi
        fi
    done < <(LC_ALL=C ipcs -s | awk '/^[0-9]/ {print $2, $3, $4}')
    echo "delete ubsmd semaphores finished"
}
remove_files() {
    systemctl daemon-reload > /dev/null 2>&1 || :
    rm -f /usr/lib/systemd/system/ubsmd.service
    rm -f /dev/shm/ubsm_records
    rm -rf /run/matrix/
}

remove_files
delete_semaphore

%files shmem
%defattr(550,ubsmd,ubsmd,550)
%attr(550,ubsmd,ubsmd) %{_bindir}/ubsmd

%attr(550,ubsmd,ubsmd) %{_libdir}/libubsm_sdk.so
%dir %attr(550,ubsmd,ubsmd) %{_prefix}/lib/ubs_mem
%attr(550,ubsmd,ubsmd) %{_prefix}/lib/ubs_mem/libubsmd.so

%dir %attr(750,ubsmd,ubsmd) %{_sysconfdir}/ubs_mem
%config(noreplace) %attr(640,ubsmd,ubsmd) %{_sysconfdir}/ubs_mem/ubsmd.conf

%attr(640,ubsmd,ubsmd) %{_includedir}/ubs_mem.h
%attr(640,ubsmd,ubsmd) %{_includedir}/ubs_mem_def.h

%attr(644,root,root) /usr/lib/systemd/system/ubsmd.service

%files
%defattr(-,root,root,-)

%changelog
