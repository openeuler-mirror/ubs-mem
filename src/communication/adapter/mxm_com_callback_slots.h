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
#ifndef MXM_COM_CALLBACK_SLOTS_H
#define MXM_COM_CALLBACK_SLOTS_H

#include <array>
#include <condition_variable>
#include <cstddef>
#include <mutex>

#include "hcom_service_c.h"

namespace ock::com {

// The handler takes ownership of the callback's Channel reference only on success (return 0).
// On failure, or when no handler is available, the slot returns that reference to HCOM.
class MxmComCallbackSlots {
public:
    using Handler = int (*)(void *engine, ubs_hcom_channel channel, const char *ipPort, const char *payload);
    static constexpr int INVALID_SLOT = -1;
    static constexpr size_t SLOT_COUNT = 4;

    static MxmComCallbackSlots &Instance();

    int Acquire(void *engine, Handler handler);
    bool Activate(int slot);
    ubs_hcom_service_channel_handler GetCallback(int slot) const;
    void BeginClose(int slot);
    // Call only after BeginClose, and never from that slot's callback.
    void WaitCallbacks(int slot);

    // The caller must first stop the old Service's callback source and all other Engine users.
    // Do not call from a callback associated with this slot.
    void ReleaseAfterServiceStopped(int slot);

private:
    enum class State { FREE, PREPARING, ACTIVE, CLOSING };
    struct Slot {
        State state{State::FREE};
        void *engine{nullptr};
        Handler handler{nullptr};
        size_t activeCallbacks{0};
    };

    MxmComCallbackSlots() = default;
    MxmComCallbackSlots(const MxmComCallbackSlots &) = delete;
    MxmComCallbackSlots &operator=(const MxmComCallbackSlots &) = delete;

    template <size_t Index>
    static int NewChannel(ubs_hcom_channel channel, const char *ipPort, uint64_t usrCtx, const char *payload);
    int Dispatch(size_t index, ubs_hcom_channel channel, const char *ipPort, const char *payload);
    void LeaveCallback(size_t index);
    bool Valid(int slot) const;

    mutable std::mutex mutex_;
    std::condition_variable callbacksDone_;
    std::array<Slot, SLOT_COUNT> slots_{};
};

} // namespace ock::com

#endif // MXM_COM_CALLBACK_SLOTS_H
