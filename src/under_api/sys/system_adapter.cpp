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

#include "system_adapter.h"
#include <numaif.h>
#include <fstream>
#include "rack_mem_functions.h"
namespace ock::ubsm {
// mmap
void *SystemAdapter::MemoryMap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    return mmap(addr, len, prot, flags, fd, offset);
}

// munmap
int SystemAdapter::MemoryUnMap(void *addr, size_t len)
{
    return munmap(addr, len);
}

// mbind
long SystemAdapter::MemoryBind(void *addr, unsigned long size, int mode, const unsigned long nodemask[],
                               unsigned long maxnode, unsigned int flags)
{
    return mbind(addr, size, mode, nodemask, maxnode, flags);
}

// open
int SystemAdapter::Open(const char *file, int oflag)
{
    return open(file, oflag);
}

// close
int SystemAdapter::Close(int fd)
{
    return close(fd);
}

// dlopen
void *SystemAdapter::DlOpen(const char *file, int mode)
{
    return dlopen(file, mode);
}

// dlsym
void *SystemAdapter::DlSym(void *handle, const char *name)
{
    return dlsym(handle, name);
}

// dlclose
int SystemAdapter::DlClose(void *handle)
{
    return dlclose(handle);
}

bool SystemAdapter::ReadUbfeatureFromSysfs(uint64_t &value)
{
    static const char *ubFeaturePath = "/sys/bus/ub/ub_feature";
    if (::access(ubFeaturePath, F_OK) != 0) {
        DBG_LOGERROR("ubfeature file not found: " << ubFeaturePath);
        return false;
    }
    if (::access(ubFeaturePath, R_OK) != 0) {
        DBG_LOGERROR("ubfeature file not readable: " << ubFeaturePath);
        return false;
    }
    std::ifstream file(ubFeaturePath);
    if (!file.is_open()) {
        DBG_LOGERROR("Failed to open ubfeature file after access check passed: " << ubFeaturePath);
        return false;
    }
    std::string line;
    if (!std::getline(file, line)) {
        DBG_LOGERROR("Failed to read ubfeature file first line: " << ubFeaturePath);
        return false;
    }
    if (!ock::mxmd::MemStrUtil::StrToULL(line, value, ock::mxmd::NO_16)) {
        DBG_LOGERROR("Failed to parse ubfeature value, first line: \"" << line << "\"");
        return false;
    }
    return true;
}
} // namespace ock::ubsm
