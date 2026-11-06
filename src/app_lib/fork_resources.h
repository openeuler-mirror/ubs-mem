/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#ifndef UBSMEM_FORK_RESOURCES_H
#define UBSMEM_FORK_RESOURCES_H
#include <sys/mman.h>
#include <atomic>
#include <cstdint>
#include "defines.h"
#include "log.h"
#include "system_adapter.h"

namespace ock::mxmd {
struct MappingSlice {
    uint64_t memId;
    void *address;
    size_t length;
    off_t deviceOffset;
    uint64_t openFlags;
    int accessFlags;
};

inline std::atomic<unsigned int> temporaryObmmFds{0};
class TemporaryObmmFd {
public:
    explicit TemporaryObmmFd(int fd) : fd_(fd)
    {
        if (fd_ >= 0) {
            temporaryObmmFds.fetch_add(1);
        }
    }
    ~TemporaryObmmFd()
    {
        if (fd_ < 0) {
            return;
        }
        ubsm::SystemAdapter::Close(fd_);
        temporaryObmmFds.fetch_sub(1);
    }
    TemporaryObmmFd(const TemporaryObmmFd &) = delete;
    TemporaryObmmFd &operator=(const TemporaryObmmFd &) = delete;

private:
    int fd_{-1};
};

inline bool DontForkMapping(void *address, size_t length)
{
    if (UNLIKELY(madvise(address, length, MADV_DONTFORK) != 0)) {
        const int failureErrno = errno;
        DBG_LOGERROR("Failed to exclude mapping from fork, address=" << address << ", length=" << length << ", advice="
                                                                     << MADV_DONTFORK << ", errno=" << failureErrno);
        return false;
    }
    return true;
}

} // namespace ock::mxmd
#endif
