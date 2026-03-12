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
#ifndef PLATFORM_UTILITIES_REMOTE_ULOG_H
#define PLATFORM_UTILITIES_REMOTE_ULOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief initialize the normal ulog
 *
 * @param logType           - [IN] type of the ulog, could 0 or 1; 0: stdout, 1: file
 * @param minLogLevel       - [IN] min level of message, 0:trace, 1:debug, 2:info, 3:warn, 4:error, 5:critical
 * @param path              - [IN] full path of ulog file name
 * @param rotationFileSize  - [IN] the max file size of a single rotation file, between 2MB to 100MB
 * @param rotationFileCount - [IN] the max count of total rotated file, less than 50
 *
 * @return 0 for success, non zero for failure
 */
int ULOG_Init(int logType, int minLogLevel, const char *path, int rotationFileSize, int rotationFileCount);

/**
 * @brief change the pattern, must be called after ULOG_Init()
 *
 * @param pattern - [IN] the pattern of the log
 *
 * @return 0 for success, non zero for failure
 */
int ULOG_SetLogPattern(const char *pattern);

/**
 * @brief ulog a message into a normal ulog
 *
 * @param logLevel         - [IN] level of the message, 0-5
 * @param prefix           - [IN] format of the message
 *
 * @return 0 for success, non zero for failure
 */
int ULOG_LogMessage(int logLevel, const char *prefix, const char *msg);

/**
 * @brief initialize the audit ulog
 *
 * @param path             - [IN] full path of ulog file name
 * @param rotationFileSize - [IN] the max file size of a single rotation file
 * @param rotationFileSize - [IN] the max count of total rotated file
 *
 * @return 0 for success, non zero for failure
 */
int ULOG_AuditInit(const char *path, int rotationFileSize, int rotationFileCount);

/**
 * @brief ulog a message into a audit ulog
 *
 * @param logLevel         - [IN] level of the message, 2:info, 3:warn, 4:error
 * @param prefix           - [IN] format of the message
 *
 * @return 0 for success, non zero for failure
 */
int ULOG_AuditLogMessage(int logLevel, const char *prefix, const char *msg);

/**
 * @brief ulog a message into a audit ulog
 *
 * @param userId           - [IN]
 * @param eventType        - [IN]
 * @param visitResource    - [IN]
 * @param msg              - [IN]
 *
 * @return 0 for success, non zero for failure
 */
int ULOG_AuditLogMessageF(const char *userId, const char *eventType, const char *visitResource, const char *msg);

/**
 * @brief retrieve the last error message
 *
 * @return message
 */
const char *ULOG_GetLastErrorMessage(void);

#if defined(__cplusplus)
}
#endif

#endif // PLATFORM_UTILITIES_REMOTE_ULOG_H
