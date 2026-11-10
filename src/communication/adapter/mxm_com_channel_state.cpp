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
#include "mxm_com_channel_state.h"

#include <utility>

namespace ock::com {

MxmComChannelState::Reference::~Reference()
{
    if (channel_ != 0) {
        ubs_hcom_channel_derefer(channel_);
    }
}

MxmComChannelState::Reference::Reference(Reference &&other) noexcept : channel_(std::exchange(other.channel_, 0)) {}

MxmComChannelState::Reference &MxmComChannelState::Reference::operator=(Reference &&other) noexcept
{
    if (this != &other) {
        if (channel_ != 0) {
            ubs_hcom_channel_derefer(channel_);
        }
        channel_ = std::exchange(other.channel_, 0);
    }
    return *this;
}

MxmComChannelState::MxmComChannelState(ubs_hcom_service service, ubs_hcom_channel channel)
    : service_(service),
      channel_(channel),
      id_(ubs_hcom_channel_get_id(channel))
{
}

MxmComChannelState::~MxmComChannelState()
{
    Close();
}

MxmComChannelState::Reference MxmComChannelState::Acquire() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (channel_ == 0) {
        return {};
    }
    ubs_hcom_channel_refer(channel_);
    return Reference(channel_);
}

void MxmComChannelState::Close()
{
    ubs_hcom_service service = 0;
    ubs_hcom_channel channel = 0;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        service = std::exchange(service_, 0);
        channel = std::exchange(channel_, 0);
    }
    if (channel != 0) {
        ubs_hcom_service_disconnect(service, channel);
    }
}

} // namespace ock::com
