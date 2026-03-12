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
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include "ulog.h"

namespace ock {
namespace utilities {
namespace log {
ULog *ULog::gLogger = nullptr;
thread_local std::string ULog::gLastErrorMessage("");
ULog *ULog::gAuditLogger = nullptr;
constexpr int ROTATION_FILE_SIZE_MAX = 100 * 1024 * 1024; // 100MB
constexpr int ROTATION_FILE_SIZE_MIN = 2 * 1024 * 1024;   // 2MB
constexpr int ROTATION_FILE_COUNT_MAX = 50;

int ULog::ValidateParams(int logType, int minLogLevel, const char *path, int rotationFileSize, int rotationFileCount)
{
    if (logType != 0 && logType != 1U && logType != 2U) {
        gLastErrorMessage = "Invalid log type, which should be 0,1,2";
        return UERR_LOG_INVALID_PARAM;
    } else if (minLogLevel < 0 || minLogLevel > MAX_LOG_LEVEL) {
        gLastErrorMessage = "Invalid min log level, which should be 0,1,2,3,4,5";
        return UERR_LOG_INVALID_PARAM;
    }

    // for stdout
    if (logType == 0) {
        return UOK;
    }

    // for file
    if (path == nullptr) {
        gLastErrorMessage = "Invalid path, which is empty";
        return UERR_LOG_INVALID_PARAM;
    } else if (rotationFileSize > ROTATION_FILE_SIZE_MAX || rotationFileSize < ROTATION_FILE_SIZE_MIN) {
        gLastErrorMessage = "Invalid max file size, which should be between 2MB to 100MB";
        return UERR_LOG_INVALID_PARAM;
    } else if (rotationFileCount > ROTATION_FILE_COUNT_MAX || rotationFileCount < 1) {
        gLastErrorMessage = "Invalid max file count, which should be less than 50";
        return UERR_LOG_INVALID_PARAM;
    }
    return UOK;
}

int ULog::CreateInstance(int logType, int minLogLevel, const char *path, int rotationFileSize, int rotationFileCount)
{
    if (gLogger != nullptr) {
        return UOK;
    }

    int hr = ValidateParams(logType, minLogLevel, path, rotationFileSize, rotationFileCount);
    if (hr != 0) {
        return hr;
    }

    std::string realPath;
    if (path != nullptr) {
        realPath = std::string(path);
    }

    auto tmpLogger = new (std::nothrow) ULog(logType, minLogLevel, realPath, rotationFileSize, rotationFileCount);
    if (tmpLogger == nullptr) {
        return UERR_LOG_CREATE_FAILED;
    }
    hr = tmpLogger->Initialize();
    if (hr != 0) {
        delete tmpLogger;
        tmpLogger = nullptr;
        return hr;
    }
    gLogger = tmpLogger;
    return UOK;
}

int ULog::LogMessage(int level, const char *prefix, const char *message)
{
    if (prefix == nullptr || message == nullptr) {
        gLastErrorMessage = "Invalid param, fmt or message is null";
        return UERR_LOG_INVALID_PARAM;
    } else if (gLogger == nullptr) {
        gLastErrorMessage = "No logger created";
        return UERR_LOG_NOT_INITIALIZED;
    }

    return gLogger->Log(level, prefix, message);
}

int ULog::SetInstancePattern(const char *pattern)
{
    if (pattern == nullptr) {
        gLastErrorMessage = "Invalid param, pattern is empty";
        return UERR_LOG_INVALID_PARAM;
    }

    if (gLogger == nullptr) {
        gLastErrorMessage = "No logger created";
        return UERR_LOG_NOT_INITIALIZED;
    }

    return gLogger->SetPattern(std::string(pattern));
}

int ULog::CreateInstanceAudit(const char *path, int rotationFileSize, int rotationFileCount)
{
    if (gAuditLogger != nullptr) {
        return UOK;
    }

    int hr = ValidateParams(1, static_cast<int>(spdlog::level::info), path, rotationFileSize, rotationFileCount);
    if (hr != 0) {
        return hr;
    }

    std::string realPath;
    if (path != nullptr) {
        realPath = std::string(path);
    }

    auto tmpLogger = new (std::nothrow) ULog(1, static_cast<int>(spdlog::level::info), realPath,
        rotationFileSize, rotationFileCount);
    if (tmpLogger == nullptr) {
        return UERR_LOG_CREATE_FAILED;
    }
    hr = tmpLogger->Initialize();
    if (hr != 0) {
        delete tmpLogger;
        tmpLogger = nullptr;
        return hr;
    }
    gAuditLogger = tmpLogger;
    return UOK;
}

int ULog::LogAuditMessage(int level, const char *prefix, const char *message)
{
    (void)level;
    if (prefix == nullptr || message == nullptr) {
        gLastErrorMessage = "Invalid param, fmt or message is null";
        return UERR_LOG_INVALID_PARAM;
    } else if (gAuditLogger == nullptr) {
        gLastErrorMessage = "No logger created";
        return UERR_LOG_NOT_INITIALIZED;
    }

    return gAuditLogger->AuditLog(message);
}

const char *ULog::GetLastErrorMessage()
{
    return gLastErrorMessage.c_str();
}

int ULog::Initialize()
{
    try {
        // create logger according to the log type
        // stdout mainly use for
        if (this->mLogType == STDOUT_TYPE) {
            this->mSPDLogger = spdlog::stdout_logger_mt("console");
            if (this->mSPDLogger.get() == nullptr) {
                gLastErrorMessage = "spdlog logger is not created yet";
                return UERR_LOG_NOT_INITIALIZED;
            }
            this->mSPDLogger->set_pattern("%Y-%m-%d %H:%M:%S.%f %t %l %v");
        } else if (this->mLogType == FILE_TYPE) {
            std::string logName = "log:" + this->mFilePath;
            this->mSPDLogger = spdlog::rotating_logger_mt(logName.c_str(), this->mFilePath, this->mRotationFileSize,
                                                          this->mRotationFileCount);
            if (this->mSPDLogger.get() == nullptr) {
                gLastErrorMessage = "spdlog logger is not created yet";
                return UERR_LOG_NOT_INITIALIZED;
            }
            this->mSPDLogger->set_pattern("%v");
            this->mSPDLogger->info("", "");
            this->mSPDLogger->set_pattern("%Y-%m-%d %H:%M:%S.%f %t %v");
            this->mSPDLogger->info("Log started at [{}] level",
                                   spdlog::level::to_string_view(static_cast<spdlog::level::level_enum>
                                   (this->mMinLogLevel)).data());
            this->mSPDLogger->info("Log default format: yyyy-mm-dd hh:mm:ss.uuuuuu threadid loglevel msg");
            this->mSPDLogger->set_pattern("%Y-%m-%d %H:%M:%S.%f %t %l %v");
            spdlog::flush_every(std::chrono::seconds(1));
        } else if (this->mLogType == STDERR_TYPE) {
            this->mSPDLogger = spdlog::stderr_logger_mt("console");
            this->mSPDLogger->set_pattern("%C/%m/%d %H:%M:%S.%f %t %l %v");
        }
        this->mSPDLogger->set_level(static_cast<spdlog::level::level_enum>(this->mMinLogLevel));
        this->mSPDLogger->flush_on(spdlog::level::err);

        if (this->mMinLogLevel < static_cast<int>(spdlog::level::info)) {
            this->mDebugEnabled = true;
        }
    } catch (const spdlog::spdlog_ex &ex) {
        gLastErrorMessage = "Failed to create log: ";
        gLastErrorMessage += ex.what();
        return UERR_LOG_CREATE_FAILED;
    } catch (...) {
        return UERR_LOG_CREATE_FAILED;
    }
    return UOK;
}

int ULog::SetPattern(const std::string &pattern)
{
    if (this->mSPDLogger.get() == nullptr) {
        gLastErrorMessage = "spdlog logger is not created yet";
        return UERR_LOG_NOT_INITIALIZED;
    }

    try {
        this->mSPDLogger->set_pattern(pattern);
    } catch (const spdlog::spdlog_ex &ex) {
        gLastErrorMessage = "Failed to set pattern to spd logger: ";
        gLastErrorMessage += ex.what();
        return UERR_LOG_SET_OPTION_FAILED;
    } catch (...) {
        return UERR_LOG_SET_OPTION_FAILED;
    }
    return UOK;
}

int ULog::Log(int level, const char *prefix, const char *message) const
{
    if (level < 0 || level > 5U) {
        gLastErrorMessage = "Invalid log level, which should be 0~5";
        return UERR_LOG_INVALID_LEVEL;
    }
    if (this->mSPDLogger.get() == nullptr) {
        gLastErrorMessage = "spdlog logger is not created yet";
        return UERR_LOG_NOT_INITIALIZED;
    }
    this->mSPDLogger->log(static_cast<spdlog::level::level_enum>(level), "{}] {}", prefix, message);
    return UOK;
}

int ULog::AuditLog(const char *message) const
{
    if (this->mSPDLogger.get() == nullptr) {
        gLastErrorMessage = "spdlog logger is not created yet";
        return UERR_LOG_NOT_INITIALIZED;
    }
    this->mSPDLogger->info("{}", message);
    return UOK;
}

void ULog::Flush(void)
{
    if (gLogger != nullptr && gLogger->mSPDLogger != nullptr) {
        gLogger->mSPDLogger->flush();
    }
    if (gAuditLogger != nullptr && gAuditLogger->mSPDLogger != nullptr) {
        gAuditLogger->mSPDLogger->flush();
    }
}

bool ULog::FrequencyLimitLog(std::chrono::time_point<std::chrono::steady_clock> &lastLogTime,
                             int32_t &curPrinted, int32_t burst, double interval)
{
    auto curLogTime = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> midTime = curLogTime - lastLogTime;
    if (midTime.count() > (interval)) {
        curPrinted = 0;
    }
    bool can = false;
    if ((burst) > curPrinted) {
        curPrinted++;
        can = true;
        lastLogTime = curLogTime;
    }
    return can;
}
}
}
}
