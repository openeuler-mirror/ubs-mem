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
#include "ulog.h"
#include "ulog4c.h"

int ULOG_Init(int logType, int minLogLevel, const char *path, int rotationFileSize, int rotationFileCount)
{
    return ock::utilities::log::ULog::CreateInstance(logType, minLogLevel, path, rotationFileSize,
                                                     rotationFileCount);
}

int ULOG_SetLogPattern(const char *pattern)
{
    return ock::utilities::log::ULog::SetInstancePattern(pattern);
}

int ULOG_LogMessage(int logLevel, const char *prefix, const char *msg)
{
    return ock::utilities::log::ULog::LogMessage(logLevel, prefix, msg);
}

int ULOG_AuditInit(const char *path, int rotationFileSize, int rotationFileCount)
{
    return ock::utilities::log::ULog::CreateInstanceAudit(path, rotationFileSize, rotationFileCount);
}

int ULOG_AuditLogMessage(int logLevel, const char *prefix, const char *msg)
{
    return ock::utilities::log::ULog::LogAuditMessage(logLevel, prefix, msg);
}

int ULOG_AuditLogMessageF(const char *userId, const char *eventType, const char *visitResource,
                          const char *msg)
{
    if (userId == nullptr || eventType == nullptr || visitResource == nullptr || msg == nullptr ||
        ock::utilities::log::ULog::gAuditLogger == nullptr ||
        ock::utilities::log::ULog::gAuditLogger->mSPDLogger == nullptr) {
        return -1;
    }
    ock::utilities::log::ULog::gAuditLogger->mSPDLogger->info("[{}][{}][{}][{}]", userId, eventType,
                                                              visitResource, msg);
    return 0;
}

const char *ULOG_GetLastErrorMessage()
{
    return ock::utilities::log::ULog::GetLastErrorMessage();
}
