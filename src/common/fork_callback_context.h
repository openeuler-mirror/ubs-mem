/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#ifndef UBSMEM_FORK_CALLBACK_CONTEXT_H
#define UBSMEM_FORK_CALLBACK_CONTEXT_H

namespace ock::mxmd {
// Shared by SDK, UBSE and external logger callbacks; never acquire a business lock here.
inline thread_local unsigned int forkCallbackDepth = 0;
inline thread_local unsigned int logCallbackDepth = 0;
class ForkCallbackScope {
public:
    explicit ForkCallbackScope(bool logCallback = false) : logCallback_(logCallback)
    {
        ++forkCallbackDepth;
        if (logCallback_) {
            ++logCallbackDepth;
        }
    }
    ~ForkCallbackScope()
    {
        if (logCallback_) {
            --logCallbackDepth;
        }
        --forkCallbackDepth;
    }
    ForkCallbackScope(const ForkCallbackScope &) = delete;
    ForkCallbackScope &operator=(const ForkCallbackScope &) = delete;

private:
    bool logCallback_;
};
} // namespace ock::mxmd
#endif
