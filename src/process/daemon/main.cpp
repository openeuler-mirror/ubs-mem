/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-mem is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ock_daemon.h"
#include "syslog.h"

using namespace ock::daemon;

namespace {
constexpr const char *UBSMD_LOCK_FILE = "/run/matrix/ubsmd.lock";
int g_lockFd = -1;
} // namespace

bool CheckIsRunning()
{
    g_lockFd = open(UBSMD_LOCK_FILE, O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (g_lockFd < 0) {
        std::cerr << "Open file " << UBSMD_LOCK_FILE << " failed, error message is " << strerror(errno) << "."
                  << std::endl;
        return true;
    }
    struct stat lockStat {};
    if (fstat(g_lockFd, &lockStat) != 0 || !S_ISREG(lockStat.st_mode) || lockStat.st_uid != getuid() ||
        lockStat.st_nlink != 1) {
        std::cerr << "Invalid ubsmd lock file." << std::endl;
        close(g_lockFd);
        g_lockFd = -1;
        return true;
    }
    flock lock{};
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len = 0;
    auto ret = fcntl(g_lockFd, F_SETLK, &lock);
    if (ret < 0) {
        std::cerr << "Fail to start ubsmd, process lock file is locked." << std::endl;
        close(g_lockFd);
        g_lockFd = -1;
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    auto start = std::chrono::steady_clock::now();
    auto uid = getuid();
    auto gid = getgid();
#if defined(_DEBUG) || defined(DEBUG)
    syslog(LOG_INFO, "(D)The current uid is %d, gid is %d.", uid, gid);
#else
    syslog(LOG_INFO, "(R)The current uid is %d, gid is %d.", uid, gid);
#endif
    if (argc != ARGS_NUM || argv == nullptr) {
        std::cerr << "Error, invalid parameters." << std::endl;
        return ERROR_EXIT_CODE;
    }
    if (CheckIsRunning()) {
        return ERROR_EXIT_CODE;
    }
    ock::daemon::OCKDaemonPtr daemon = new (std::nothrow) ock::daemon::OckDaemon();
    if (daemon == nullptr) {
        std::cerr << "Failed to new ock daemon, maybe out of memory" << std::endl;
        return ERROR_EXIT_CODE;
    }
    if (daemon->CheckParam(argv[RUNTIME_PATH_POSITION], argv[CONFIG_PATH_POSITION]) != 0) {
        daemon.Set(nullptr);
        return ERROR_EXIT_CODE;
    }
    if (daemon->Initialize() != 0) {
        daemon.Set(nullptr);
        return ERROR_EXIT_CODE;
    }
    if (daemon->Start(start) != 0) {
        DBG_LOGERROR("Failed to start OckDaemon");
        daemon.Set(nullptr);
        ock::daemon::OckDaemon::PrintStartTime(start, "START_FAILED");
        return ERROR_EXIT_CODE;
    }
    if (daemon->Uninitialize() != 0) {
        daemon.Set(nullptr);
        DBG_LOGERROR("Failed to uninitialize ock daemon");
        return ERROR_EXIT_CODE;
    }
    daemon.Set(nullptr);
    ock::daemon::OckDaemon::PrintStartTime(start, "STOP");
    return CLEAN_EXIT_CODE;
}
