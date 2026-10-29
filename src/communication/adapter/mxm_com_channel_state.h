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
#ifndef MXM_COM_CHANNEL_STATE_H
#define MXM_COM_CHANNEL_STATE_H

#include <memory>
#include <mutex>

#include "hcom_service_c.h"

namespace ock::com {

// One state owns exactly one Channel reference returned by Connect or a NewChannel callback.
// All indexes and copied channel descriptions must share this same state.
class MxmComChannelState {
public:
    class Reference {
    public:
        Reference() = default;
        ~Reference();
        Reference(const Reference &) = delete;
        Reference &operator=(const Reference &) = delete;
        Reference(Reference &&other) noexcept;
        Reference &operator=(Reference &&other) noexcept;

        ubs_hcom_channel Get() const
        {
            return channel_;
        }
        explicit operator bool() const
        {
            return channel_ != 0;
        }

    private:
        friend class MxmComChannelState;
        explicit Reference(ubs_hcom_channel channel) : channel_(channel) {}
        ubs_hcom_channel channel_{0};
    };

    MxmComChannelState(ubs_hcom_service service, ubs_hcom_channel channel);
    ~MxmComChannelState();
    MxmComChannelState(const MxmComChannelState &) = delete;
    MxmComChannelState &operator=(const MxmComChannelState &) = delete;

    // Acquire a temporary reference for an operation that may overlap Close.
    Reference Acquire() const;
    // Consume the owned reference exactly once. The Service must still be alive.
    void Close();

private:
    mutable std::mutex mutex_;
    ubs_hcom_service service_{0};
    ubs_hcom_channel channel_{0};
};

using MxmComChannelStatePtr = std::shared_ptr<MxmComChannelState>;

} // namespace ock::com

#endif // MXM_COM_CHANNEL_STATE_H
