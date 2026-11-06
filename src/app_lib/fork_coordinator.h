/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#ifndef UBSMEM_FORK_COORDINATOR_H
#define UBSMEM_FORK_COORDINATOR_H

#include <sys/types.h>
#include <atomic>
#include <cstdint>
#include "ubs_mem_def.h"
#include "fork_callback_context.h"
#include "log.h"

namespace ock::mxmd {
class ForkCoordinator {
public:
    static ForkCoordinator &Instance() noexcept;
    void SetInitialized(bool initialized);
    bool EnterApi();
    void LeaveApi();
    int Prepare();
    int Parent();
    int Child();

private:
    enum class Phase : uint32_t {
        RUNNING,
        PREPARING,
        PREPARED,
        RECOVERING,
        PREPARE_FAILED,
        PARENT_FAILED,
        CHILD_FAILED
    };
    static_assert(std::atomic<Phase>::is_always_lock_free);
    constexpr ForkCoordinator() noexcept = default;
    static ForkCoordinator instance_;
    std::atomic<pid_t> runtimePid_{0};
    bool parentIpcReady_{false};
    static constexpr uint64_t CLOSED = uint64_t{1} << 63;
    // Admission and closing share a CAS, so prepare cannot miss an entering API.
    std::atomic<uint64_t> admission_{0};
    std::atomic<Phase> phase_{Phase::RUNNING};
    std::atomic<pid_t> ownerPid_{0};
    bool childReset_{false};
    std::atomic<uint64_t> generation_{0};
    void Open();
    int ResumeParent();
};

class ForkApiGuard {
public:
    ForkApiGuard() : entered_(ForkCoordinator::Instance().EnterApi()) {}
    ~ForkApiGuard()
    {
        if (entered_) {
            ForkCoordinator::Instance().LeaveApi();
        }
    }
    bool Entered() const
    {
        return entered_;
    }
    ForkApiGuard(const ForkApiGuard &) = delete;
    ForkApiGuard &operator=(const ForkApiGuard &) = delete;

private:
    bool entered_;
};
} // namespace ock::mxmd

// Rejected calls are outside the drained set and must not acquire logger/runtime locks.
#define UBSMEM_API_GUARD()                \
    ock::mxmd::ForkApiGuard forkApiGuard; \
    if (!forkApiGuard.Entered()) {        \
        return UBSM_ERR_BUSY;             \
    }
#endif
