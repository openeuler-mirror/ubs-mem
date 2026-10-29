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
#include "mxm_com_callback_slots.h"

namespace ock::com {

MxmComCallbackSlots &MxmComCallbackSlots::Instance()
{
    static MxmComCallbackSlots slots;
    return slots;
}

bool MxmComCallbackSlots::Valid(int slot) const
{
    return slot >= 0 && static_cast<size_t>(slot) < SLOT_COUNT;
}

int MxmComCallbackSlots::Acquire(void *engine, Handler handler)
{
    if (engine == nullptr || handler == nullptr) {
        return INVALID_SLOT;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    for (const Slot &entry : slots_) {
        if (entry.state != State::FREE && entry.engine == engine) {
            return INVALID_SLOT;
        }
    }
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (slots_[i].state == State::FREE) {
            slots_[i] = {State::PREPARING, engine, handler, 0};
            return static_cast<int>(i);
        }
    }
    return INVALID_SLOT;
}

bool MxmComCallbackSlots::Activate(int slot)
{
    if (!Valid(slot)) {
        return false;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    Slot &entry = slots_[slot];
    if (entry.state != State::PREPARING) {
        return false;
    }
    entry.state = State::ACTIVE;
    return true;
}

ubs_hcom_service_channel_handler MxmComCallbackSlots::GetCallback(int slot) const
{
    static constexpr std::array<ubs_hcom_service_channel_handler, SLOT_COUNT> callbacks = {
        &NewChannel<0>, &NewChannel<1>, &NewChannel<2>, &NewChannel<3>};
    return Valid(slot) ? callbacks[slot] : nullptr;
}

void MxmComCallbackSlots::BeginClose(int slot)
{
    if (!Valid(slot)) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    if (slots_[slot].state == State::PREPARING || slots_[slot].state == State::ACTIVE) {
        slots_[slot].state = State::CLOSING;
    }
}

void MxmComCallbackSlots::WaitCallbacks(int slot)
{
    if (!Valid(slot)) {
        return;
    }
    std::unique_lock<std::mutex> guard(mutex_);
    callbacksDone_.wait(guard, [this, slot] { return slots_[slot].activeCallbacks == 0; });
}

void MxmComCallbackSlots::ReleaseAfterServiceStopped(int slot)
{
    if (!Valid(slot)) {
        return;
    }
    std::unique_lock<std::mutex> guard(mutex_);
    if (slots_[slot].state != State::CLOSING) {
        return;
    }
    callbacksDone_.wait(guard, [this, slot] { return slots_[slot].activeCallbacks == 0; });
    slots_[slot] = {};
}

void MxmComCallbackSlots::LeaveCallback(size_t index)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (--slots_[index].activeCallbacks == 0) {
        callbacksDone_.notify_all();
    }
}

int MxmComCallbackSlots::Dispatch(size_t index, ubs_hcom_channel channel, const char *ipPort, const char *payload)
{
    Handler handler = nullptr;
    void *engine = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        Slot &entry = slots_[index];
        ++entry.activeCallbacks;
        if (entry.state == State::ACTIVE) {
            handler = entry.handler;
            engine = entry.engine;
        }
    }

    int result = -1;
    try {
        if (handler != nullptr) {
            result = handler(engine, channel, ipPort, payload);
        }
    } catch (...) {
        result = -1; // Never let a C++ exception cross the C callback boundary.
    }
    if (result != 0) {
        ubs_hcom_channel_derefer(channel);
    }
    LeaveCallback(index);
    return result;
}

template <size_t Index>
int MxmComCallbackSlots::NewChannel(ubs_hcom_channel channel, const char *ipPort, uint64_t, const char *payload)
{
    return Instance().Dispatch(Index, channel, ipPort, payload);
}

} // namespace ock::com
