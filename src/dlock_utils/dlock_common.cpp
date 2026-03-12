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

#include <string>
#include <vector>
#include <unordered_map>
#include "log.h"
#include "dlock_common.h"

namespace ock {
namespace dlock_utils {
const std::string& GetReinitStageName(REINIT_STAGES stage)
{
    static const std::vector<std::string> names = {
        "CLIENT_REINIT",
        "UPDATE_LOCK",
        "CLIENT_REINIT_DONE"
    };
    static const std::string unknownStage = "UNKNOWN_STAGE";
    if (static_cast<int>(stage) >= names.size()) {
        return unknownStage;
    }
    return names[static_cast<int>(stage)];
}

DLockLogLevel MapUbsmLogLevel2DLockLevel(int32_t level)
{
    static const std::unordered_map<int32_t, DLockLogLevel> levelMap = {
        {static_cast<int32_t>(ock::utilities::log::LogLevel::DEBUG), DLOCK_LOG_LEVEL_DEBUG},
        {static_cast<int32_t>(ock::utilities::log::LogLevel::INFO), DLOCK_LOG_LEVEL_INFO},
        {static_cast<int32_t>(ock::utilities::log::LogLevel::WARN), DLOCK_LOG_LEVEL_WARNING},
        {static_cast<int32_t>(ock::utilities::log::LogLevel::ERROR), DLOCK_LOG_LEVEL_ERR},
        {static_cast<int32_t>(ock::utilities::log::LogLevel::CRITICAL), DLOCK_LOG_LEVEL_CRIT}};
    auto it = levelMap.find(level);
    if (it == levelMap.end()) {
        DBG_LOGWARN("Cannot find loglevel=" << level << ", use dlock info loglevel=" << DLOCK_LOG_LEVEL_INFO);
        return DLOCK_LOG_LEVEL_INFO;
    }
    return it->second;
}
} // namespace dlock_utils
} // namespace ock
