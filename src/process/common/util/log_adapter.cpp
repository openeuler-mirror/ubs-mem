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
#include "log_adapter.h"
#include "functions.h"
#include "ulog/log.h"
#include "configuration.h"

namespace ock {
namespace common {
Lock LogAdapter::gLock;
Lock LogAdapter::gAuditLock;
uint32_t LogAdapter::gLevel = 6L;


HRESULT LogAdapter::LogServerInit(int minLogLevel, const std::string &path, int rotationFileSize, int rotationFileCount)
{
    GUARD(&gLock, gLock);

    gLevel = static_cast<uint32_t>(minLogLevel);
    std::string hostname = OckGetHostname();
    std::string logFileFullPath = path;
    if (logFileFullPath.empty()) {
        logFileFullPath = ConfConstant::MXMD_SERVER_LOG_PATH.second;
    }
    if (logFileFullPath.at(logFileFullPath.size() - 1) == '/') {
        logFileFullPath += "ubsmd.log";
    } else {
        logFileFullPath += "/ubsmd.log";
    }

    int ret = ULOG_Init(ock::utilities::log::FILE_TYPE, minLogLevel, logFileFullPath.c_str(), rotationFileSize,
        rotationFileCount);
    if (ret != 0) {
        return HFAIL;
    }
    DBG_LOGINFO("Server log created successfully.");

    return HOK;
}

HRESULT LogAdapter::AuditLogInit(const std::string &path, int rotationFileSize, int rotationFileCount)
{
    GUARD(&gAuditLock, gAuditLock);

    std::string hostname = OckGetHostname();
    std::string auditLogFileFullPath = path;
    if (auditLogFileFullPath.empty()) {
        auditLogFileFullPath = ConfConstant::MXMD_SERVER_AUDIT_LOG_PATH.second;
    }
    if (auditLogFileFullPath.at(auditLogFileFullPath.size() - 1) == '/') {
        auditLogFileFullPath += "ubsmd.audit.log";
    } else {
        auditLogFileFullPath += "/ubsmd.audit.log";
    }
    int ret = ULOG_AuditInit(auditLogFileFullPath.c_str(), rotationFileSize, rotationFileCount);
    if (ret != 0) {
        DBG_LOGWARN("Failed to create audit log with ret" << ret);
        return HFAIL;
    }

    DBG_LOGINFO("Audit log created successfully.");

    return HOK;
}

inline int32_t MapStringToLevel(const std::string &level)
{
    if (level.compare("DEBUG") == 0) {
        return static_cast<int32_t>(DBG_LOG_DEBUG);
    }
    if (level.compare("INFO") == 0) {
        return static_cast<int32_t>(DBG_LOG_INFO);
    }
    if (level.compare("WARN") == 0) {
        return static_cast<int32_t>(DBG_LOG_WARN);
    }
    if (level.compare("ERROR") == 0) {
        return static_cast<int32_t>(DBG_LOG_ERROR);
    }
    if (level.compare("CRITICAL") == 0) {
        return static_cast<int32_t>(DBG_LOG_CRITICAL);
    }
    return -1;
}

int32_t LogAdapter::StringToLogLevel(const std::string &level)
{
    auto logLevel = MapStringToLevel(level);
    if (logLevel >= 0) {
        return logLevel;
    }

    DBG_LOGERROR("Configuration module: The log level is incorrect. Use log level: INFO.");
    return static_cast<int32_t>(ock::utilities::log::LogLevel::INFO);
}
} // namespace common
} // namespace ock