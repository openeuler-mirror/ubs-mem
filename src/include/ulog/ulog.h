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

#ifndef PLATFORM_UTILITIES_REMOTE_LOGGING_H
#define PLATFORM_UTILITIES_REMOTE_LOGGING_H

#include <cstdint>
#include <string>
#include <sstream>
#include <chrono>
#include <spdlog/spdlog.h>

namespace ock {
namespace utilities {
namespace log {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    CRITICAL = 5,
    LOG_LEVEL_MAX,
};

constexpr int UOK = 0;
constexpr int UERR_LOG_CREATE_FAILED = 1;
constexpr int UERR_LOG_INVALID_PARAM = 2;
constexpr int UERR_LOG_NOT_INITIALIZED = 3;
constexpr int UERR_LOG_SET_OPTION_FAILED = 4;
constexpr int UERR_LOG_INVALID_LEVEL = 5;

constexpr int STDOUT_TYPE = 0;
constexpr int FILE_TYPE = 1;
constexpr int STDERR_TYPE = 2;

constexpr int MAX_LOG_LEVEL = static_cast<int>(LogLevel::LOG_LEVEL_MAX);
constexpr int MIN_LOG_LEVEL = 1;
class ULog {
public:
    ULog(int logType, int minLogLevel, const std::string& path, int rotationFileSize, int rotationFileCount)
        : mLogType(logType),
          mMinLogLevel(minLogLevel),
          mFilePath(path),
          mRotationFileSize(rotationFileSize),
          mRotationFileCount(rotationFileCount)
    {
    }

    ~ULog() = default;

    int Initialize();
    int SetPattern(const std::string& pattern);

    inline bool IsDebug() const { return mDebugEnabled; }

    inline bool IsHigherLevel(int nowLevel) const { return nowLevel >= mMinLogLevel; }

    int Log(int level, const char* prefix, const char* message) const;
    int AuditLog(const char* message) const;

    static bool FrequencyLimitLog(std::chrono::time_point<std::chrono::steady_clock>& lastLogTime, int32_t& curPrinted,
                                  int32_t burst, double interval);
    /* *
     * @brief This is not thread safe, need to be called at the first place of main thread
     */
    static int CreateInstance(int logType, int minLogLevel, const char* path, int rotationFileSize,
                              int rotationFileCount);
    static int LogMessage(int level, const char* prefix, const char* message);
    static int SetInstancePattern(const char* pattern);
    static ULog* gLogger;
    static ULog* gAuditLogger;

    /* *
     * @brief This is not thread safe, need to be called at the first place of main thread
     */
    static int CreateInstanceAudit(const char* path, int rotationFileSize, int rotationFileCount);
    static int LogAuditMessage(int level, const char* prefix, const char* message);

    static const char* GetLastErrorMessage();

    // spd logger
    std::shared_ptr<spdlog::logger> mSPDLogger;

    // spd flush
    static void Flush(void);

private:
    static int ValidateParams(int logType, int minLogLevel, const char* path, int rotationFileSize,
                              int rotationFileCount);

private:
    // log options
    int mLogType;
    int mMinLogLevel;
    std::string mFilePath;
    int mRotationFileSize;
    int mRotationFileCount;
    bool mDebugEnabled = false;

    static thread_local std::string gLastErrorMessage;
};
}  // namespace log
}  // namespace utilities
}  // namespace ock
inline bool CheckLogger()
{
    if (ock::utilities::log::ULog::gLogger == nullptr || ock::utilities::log::ULog::gLogger->mSPDLogger == nullptr) {
        return false;
    }
    return true;
}

inline bool CheckAuditLogger()
{
    if (ock::utilities::log::ULog::gAuditLogger == nullptr ||
        ock::utilities::log::ULog::gAuditLogger->mSPDLogger == nullptr) {
        return false;
    }
    return true;
}

// audit log
#define ULOG_AUDIT_CRITICAL(fmt, ...) ULOG_AUDIT_INTERNAL(spdlog::level::critical, fmt, ##__VA_ARGS__)
#define ULOG_AUDIT_ERROR(fmt, ...) ULOG_AUDIT_INTERNAL(spdlog::level::err, fmt, ##__VA_ARGS__)
#define ULOG_AUDIT_WARN(fmt, ...) ULOG_AUDIT_INTERNAL(spdlog::level::warn, fmt, ##__VA_ARGS__)
#define ULOG_AUDIT_INFO(fmt, ...) ULOG_AUDIT_INTERNAL(spdlog::level::info, fmt, ##__VA_ARGS__)

#define ULOG_AUDIT_INTERNAL(level, fmt, ...)                                                       \
    do {                                                                                           \
        if (!CheckAuditLogger()) {                                                                 \
            break;                                                                                 \
        }                                                                                          \
        static thread_local std::ostringstream oss;                                                \
        oss.str("");                                                                               \
        oss.clear();                                                                               \
        oss << fmt;                                                                                \
        ock::utilities::log::ULog::gAuditLogger->mSPDLogger->log(level, oss.str(), ##__VA_ARGS__); \
        ock::utilities::log::ULog::gAuditLogger->mSPDLogger->flush();                              \
    } while (0)

// universal log
#ifndef __ULOG_FILENAME__
#define __ULOG_FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define ULOG_UNITY_CRITICAL(fmt, ...) ULOG_UNITY_ARGS_INTERNAL(spdlog::level::critical, fmt, ##__VA_ARGS__)
#define ULOG_UNITY_ERROR(fmt, ...) ULOG_UNITY_ARGS_INTERNAL(spdlog::level::err, fmt, ##__VA_ARGS__)
#define ULOG_UNITY_WARN(fmt, ...) ULOG_UNITY_ARGS_INTERNAL(spdlog::level::warn, fmt, ##__VA_ARGS__)
#define ULOG_UNITY_INFO(fmt, ...) ULOG_UNITY_ARGS_INTERNAL(spdlog::level::info, fmt, ##__VA_ARGS__)

#define ULOG_UNITY_ARGS_INTERNAL(level, fmt, ...)                                                               \
    do {                                                                                                        \
        if (!CheckLogger() || !ock::utilities::log::ULog::gLogger->IsHigherLevel(level)) {                      \
            break;                                                                                              \
        }                                                                                                       \
        static thread_local std::ostringstream oss;                                                             \
        oss.str("");                                                                                            \
        oss.clear();                                                                                            \
        oss << "[" << std::this_thread::get_id() << "] " << "[" << __ULOG_FILENAME__ << ":" << __LINE__ << "] " \
            << fmt;                                                                                             \
        ock::utilities::log::ULog::gLogger->mSPDLogger->log(level, oss.str(), ##__VA_ARGS__);                   \
        ock::utilities::log::ULog::gLogger->mSPDLogger->flush();                                                \
    } while (0)

#define ULOG_UNITY_DEBUG(fmt, ...)                                                                              \
    do {                                                                                                        \
        if (!CheckLogger() || !ock::utilities::log::ULog::gLogger->IsDebug()) {                                 \
            break;                                                                                              \
        }                                                                                                       \
        static thread_local std::ostringstream oss;                                                             \
        oss.str("");                                                                                            \
        oss.clear();                                                                                            \
        oss << "[" << std::this_thread::get_id() << "] " << "[" << __ULOG_FILENAME__ << ":" << __LINE__ << "] " \
            << fmt;                                                                                             \
        ock::utilities::log::ULog::gLogger->mSPDLogger->debug(oss.str(), ##__VA_ARGS__);                        \
        ock::utilities::log::ULog::gLogger->mSPDLogger->flush();                                                \
    } while (0)

// Frequency limit log function

#define ULOG_UNITY_CRITICAL_LIMIT(fmt, interval, burst, ...) \
    ULOG_UNITY_LIMIT(spdlog::level::critical, fmt, interval, burst, ##__VA_ARGS__)

#define ULOG_UNITY_ERROR_LIMIT(fmt, interval, burst, ...) \
    ULOG_UNITY_LIMIT(spdlog::level::err, fmt, interval, burst, ##__VA_ARGS__)

#define ULOG_UNITY_WARN_LIMIT(fmt, interval, burst, ...) \
    ULOG_UNITY_LIMIT(spdlog::level::warn, fmt, interval, burst, ##__VA_ARGS__)

#define ULOG_UNITY_INFO_LIMIT(fmt, interval, burst, ...) \
    ULOG_UNITY_LIMIT(spdlog::level::info, fmt, interval, burst, ##__VA_ARGS__)

#define ULOG_UNITY_LIMIT(level, fmt, interval, burst, ...)                                                        \
    do {                                                                                                          \
        static int32_t curPrinted = 0;                                                                            \
        static std::chrono::time_point<std::chrono::steady_clock> lastLogTime = std::chrono::steady_clock::now(); \
        bool can = ock::utilities::log::ULog::FrequencyLimitLog(lastLogTime, curPrinted, burst, interval);        \
        if (can) {                                                                                                \
            ULOG_UNITY_ARGS_INTERNAL(level, fmt, ##__VA_ARGS__);                                                  \
        }                                                                                                         \
    } while (0)

#define ULOG_UNITY_DEBUG_LIMIT(fmt, interval, burst, ...)                                                         \
    do {                                                                                                          \
        static int32_t curPrinted = 0;                                                                            \
        static std::chrono::time_point<std::chrono::steady_clock> lastLogTime = std::chrono::steady_clock::now(); \
        bool can = ock::utilities::log::ULog::FrequencyLimitLog(lastLogTime, curPrinted, burst, interval);        \
        if (can) {                                                                                                \
            ULOG_UNITY_DEBUG(fmt, ##__VA_ARGS__);                                                                 \
        }                                                                                                         \
    } while (0)

#endif  // PLATFORM_UTILITIES_REMOTE_LOGGING_H
