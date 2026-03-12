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
#ifndef LOG_H
#define LOG_H

#ifdef CLIENT_LOG
#include "dg_out_logger.h"
#else
#include "ulog.h"
#endif

#define DBG_LOG_TRACE 0L
#define DBG_LOG_DEBUG 1L
#define DBG_LOG_INFO 2L
#define DBG_LOG_WARN 3L
#define DBG_LOG_ERROR 4L
#define DBG_LOG_CRITICAL 5L
#define DBG_LOG_LEVEL_MAX 6L

#ifdef CLIENT_LOG
#define DBG_LOGCRITICAL(fmt, ...) DAGGER_OUT_LOG(ock::dagger::LogLevel::CRITICAL_LEVEL, UBS_SDK, fmt)
#else
#define DBG_LOGCRITICAL(fmt, ...) ULOG_UNITY_CRITICAL(fmt, ##__VA_ARGS__)
#endif

#ifndef DEBUG_MEM_UT
#ifdef CLIENT_LOG
#define DBG_LOGERROR(fmt, ...) DAGGER_OUT_LOG(ock::dagger::LogLevel::ERROR_LEVEL, UBS_SDK, fmt)
#else
#define DBG_LOGERROR(fmt, ...) ULOG_UNITY_ERROR(fmt, ##__VA_ARGS__)
#endif
#else
#define DBG_LOGERROR(fmt, ...) (void)0
#endif

#ifndef DEBUG_MEM_UT
#ifdef CLIENT_LOG
#define DBG_LOGINFO(fmt, ...) DAGGER_OUT_LOG(ock::dagger::LogLevel::INFO_LEVEL, UBS_SDK, fmt)
#else
#define DBG_LOGINFO(fmt, ...) ULOG_UNITY_INFO(fmt, ##__VA_ARGS__)
#endif
#else
#define DBG_LOGINFO(fmt, ...) (void)0
#endif
 
#ifndef DEBUG_MEM_UT
#ifdef CLIENT_LOG
#define DBG_LOGWARN(fmt, ...) DAGGER_OUT_LOG(ock::dagger::LogLevel::WARN_LEVEL, UBS_SDK, fmt)
#else
#define DBG_LOGWARN(fmt, ...) ULOG_UNITY_WARN(fmt, ##__VA_ARGS__)
#endif
#else
#define DBG_LOGWARN(fmt, ...) (void)0
#endif
 
#ifndef DEBUG_MEM_UT
#ifdef CLIENT_LOG
#define DBG_LOGDEBUG(fmt, ...) DAGGER_OUT_LOG(ock::dagger::LogLevel::DEBUG_LEVEL, UBS_SDK, fmt)
#else
#define DBG_LOGDEBUG(fmt, ...) ULOG_UNITY_DEBUG(fmt, ##__VA_ARGS__)
#endif
#else
#define DBG_LOGDEBUG(fmt, ...) (void)0
#endif


#define DBG_LOGCRITICAL_LIMIT(fmt, interval, burst, ...) ULOG_UNITY_CRITICAL_LIMIT(fmt, interval, burst, ##__VA_ARGS__)

#define DBG_LOGERROR_LIMIT(fmt, interval, burst, ...) ULOG_UNITY_ERROR_LIMIT(fmt, interval, burst, ##__VA_ARGS__)

#define DBG_LOGWARN_LIMIT(fmt, interval, burst, ...) ULOG_UNITY_WARN_LIMIT(fmt, interval, burst, ##__VA_ARGS__)

#define DBG_LOGINFO_LIMIT(fmt, interval, burst, ...) ULOG_UNITY_INFO_LIMIT(fmt, interval, burst, ##__VA_ARGS__)

#define DBG_LOGDEBUG_LIMIT(fmt, interval, burst, ...) ULOG_UNITY_DEBUG_LIMIT(fmt, interval, burst, ##__VA_ARGS__)

#define DBG_AUDITERROR(fmt, ...) ULOG_AUDIT_ERROR(fmt, ##__VA_ARGS__)

#define DBG_AUDITWARN(fmt, ...) ULOG_AUDIT_WARN(fmt, ##__VA_ARGS__)

#define DBG_AUDITINFO(fmt, ...) ULOG_AUDIT_INFO(fmt, ##__VA_ARGS__)

#define DBG_AUDITCRITICAL(fmt, ...) ULOG_AUDIT_CRITICAL(fmt, ##__VA_ARGS__)

#endif