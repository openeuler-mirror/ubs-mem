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
#ifndef HDAGGER_DG_DAGGER_OUT_LOGGER_H
#define HDAGGER_DG_DAGGER_OUT_LOGGER_H

#include <ctime>
#include <cstring>
#include <iostream>
#include <mutex>
#include <unistd.h>
#include <sys/types.h>
#include <sstream>
#include <sys/time.h>
#include <iomanip>
#include <sys/syscall.h>

#include "../dg_common.h"

namespace ock {
namespace dagger {
#ifndef DAGGER_DAGGER_OUT_LOGGER
typedef void (*ExternalLog)(int level, const char *msg);
#endif

enum LogLevel : int {
    DEBUG_LEVEL = 0,
    INFO_LEVEL,
    WARN_LEVEL,
    ERROR_LEVEL,
    CRITICAL_LEVEL,
    BUTT_LEVEL
};
constexpr int MS_WIDTH = 3;
constexpr int HOUR_WIDTH = 2;
constexpr int MINUTE_WIDTH = 2;
constexpr int MINUTES_PER_HOUR = 60;
constexpr int SECOND_PER_HOUR = 3600;

class OutLogger {
public:
    static OutLogger *Instance()
    {
        static OutLogger *gLogger = nullptr;
        static std::mutex gMutex;

        if (__builtin_expect(gLogger == nullptr, 0) != 0) {
            gMutex.lock();
            if (gLogger == nullptr) {
                gLogger = new (std::nothrow) OutLogger();

                if (gLogger == nullptr) {
                    printf("Failed to new OutLogger, probably out of memory");
                }
            }
            gMutex.unlock();
        }

        return gLogger;
    }

    inline void SetLogLevel(LogLevel level)
    {
        mLogLevel = level;
    }

    inline void SetAuditLogLevel(LogLevel level)
    {
        mAuditLogLevel = level;
    }

    inline void SetExternalLogFunction(ExternalLog func, bool forceUpdate = false)
    {
        if (func != nullptr || forceUpdate) {
            mLogFunc = func;
        }
    }

    ExternalLog GetExternalLogFunction()
    {
        return mLogFunc;
    }

    inline void SetExternalAuditLogFunction(ExternalLog func, bool forceUpdate = false)
    {
        if (func != nullptr || forceUpdate) {
            mAuditLogFunc = func;
        }
    }

    static std::string GetTimeStamp()
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        time_t timeStamp = tv.tv_sec;
        struct tm localTime;
        localtime_r(&timeStamp, &localTime);

        char timeBuf[32];
        if (strftime(timeBuf, sizeof timeBuf, "%Y-%m-%d %H:%M:%S", &localTime) == 0) {
            return "[Invalid Time]";
        }

        int ms = static_cast<int>(tv.tv_usec / 1000);
        std::ostringstream oss;
        oss << timeBuf << "." << std::setfill('0') << std::setw(MS_WIDTH) << ms << " ";
        long offsetSec = localTime.tm_gmtoff;
        int offsetHours = static_cast<int>(offsetSec / SECOND_PER_HOUR);
        int offsetMinutes = static_cast<int>(std::abs(offsetSec % SECOND_PER_HOUR) / MINUTES_PER_HOUR);

        if (offsetHours >= 0) {
            oss << '+' << std::setfill('0') << std::setw(HOUR_WIDTH) << offsetHours;
        } else {
            oss << '-' << std::setfill('0') << std::setw(HOUR_WIDTH) << (-offsetHours);
        }
        oss << ':' << std::setfill('0') << std::setw(MINUTE_WIDTH) << offsetMinutes;
        return oss.str();
    }

    inline void Log(int level, const std::ostringstream &oss)
    {
        if (level < mLogLevel) {
            return;
        }
        if (mLogFunc != nullptr) {
            mLogFunc(level, oss.str().c_str());
            return;
        }

        std::cout << "[" << GetTimeStamp() << "][" << LogLevelDesc(level) << "][" << getpid() << "][" << pthread_self()
                  << "] " << oss.str() << std::endl;
    }

    inline void AuditLog(int level, const std::ostringstream &oss)
    {
        if (mAuditLogFunc != nullptr) {
            mAuditLogFunc(level, oss.str().c_str());
            return;
        }

        if (level < mAuditLogLevel) {
            return;
        }

        std::cout << "[" << GetTimeStamp() << "][" << LogLevelDesc(level) << "][" << getpid() << "][" << pthread_self()
                  << "] " << oss.str() << std::endl;
    }

    OutLogger(const OutLogger &) = delete;
    OutLogger(OutLogger &&) = delete;

    ~OutLogger()
    {
        mLogFunc = nullptr;
        mAuditLogFunc = nullptr;
    }

private:
    OutLogger() = default;

    inline const std::string &LogLevelDesc(int level)
    {
        static std::string invalid = "invalid";
        if (DAGGER_UNLIKELY(level < DEBUG_LEVEL || level >= BUTT_LEVEL)) {
            return invalid;
        }
        return mLogLevelDesc[level];
    }

private:
    const std::string mLogLevelDesc[BUTT_LEVEL] = {"DEBUG", "INFO", "WARN", "ERROR"};

    LogLevel mLogLevel = DEBUG_LEVEL;
    LogLevel mAuditLogLevel = INFO_LEVEL;
    ExternalLog mLogFunc = nullptr;
    ExternalLog mAuditLogFunc = nullptr;
};

// macro for log
#ifndef DAGGER_FILENAME_SHORT
#define DAGGER_FILENAME_SHORT (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif
#define DAGGER_OUT_LOG(LEVEL, MODULE, ARGS)                                                       \
    do {                                                                                          \
        std::ostringstream oss;                                                                   \
        oss << "[" << #MODULE << " " << DAGGER_FILENAME_SHORT << ":" << __LINE__ << "] " << ARGS; \
        ock::dagger::OutLogger::Instance()->Log(LEVEL, oss);                                                   \
    } while (0)

#define DAGGER_AUDIT_OUT_LOG(LEVEL, MODULE, ARGS)    \
    do {                                             \
        std::ostringstream oss;                      \
        oss << "[AUDIT " << #MODULE << "] " << ARGS; \
        ock::dagger::OutLogger::Instance()->AuditLog(LEVEL, oss); \
    } while (0)

#define DAGGER_LOG_DEBUG(MODULE, ARGS) DAGGER_OUT_LOG(DEBUG_LEVEL, MODULE, ARGS)
#define DAGGER_LOG_INFO(MODULE, ARGS) DAGGER_OUT_LOG(INFO_LEVEL, MODULE, ARGS)
#define DAGGER_LOG_WARN(MODULE, ARGS) DAGGER_OUT_LOG(WARN_LEVEL, MODULE, ARGS)
#define DAGGER_LOG_ERROR(MODULE, ARGS) DAGGER_OUT_LOG(ERROR_LEVEL, MODULE, ARGS)

#define DAGGER_AUDIT_LOG_INFO(MODULE, ARGS) DAGGER_AUDIT_OUT_LOG(INFO_LEVEL, MODULE, ARGS)
#define DAGGER_AUDIT_LOG_WARN(MODULE, ARGS) DAGGER_AUDIT_OUT_LOG(WARN_LEVEL, MODULE, ARGS)
#define DAGGER_AUDIT_LOG_ERROR(MODULE, ARGS) DAGGER_AUDIT_OUT_LOG(ERROR_LEVEL, MODULE, ARGS)

#define DAGGER_ASSERT_RETURN(MODULE, ARGS, RET)           \
    do {                                                  \
        if (__builtin_expect(!(ARGS), 0) != 0) {          \
            DAGGER_LOG_ERROR(MODULE, "Assert " << #ARGS); \
            return RET;                                   \
        }                                                 \
    } while (0)

#define DAGGER_ASSERT_RET_VOID(MODULE, ARGS)              \
    do {                                                  \
        if (__builtin_expect(!(ARGS), 0) != 0) {          \
            DAGGER_LOG_ERROR(MODULE, "Assert " << #ARGS); \
            return;                                       \
        }                                                 \
    } while (0)

#define DAGGER_ASSERT(MODULE, ARGS)                       \
    do {                                                  \
        if (__builtin_expect(!(ARGS), 0) != 0) {          \
            DAGGER_LOG_ERROR(MODULE, "Assert " << #ARGS); \
        }                                                 \
    } while (0)
}
}

#endif // HDAGGER_DG_DAGGER_OUT_LOGGER_H
