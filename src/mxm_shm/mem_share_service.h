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
#ifndef IOFWD_FORWARD_SERVICE_SHM_H
#define IOFWD_FORWARD_SERVICE_SHM_H

#include <functional>
#include <utility>
#include <vector>
#include "log.h"
#include "service.h"

namespace ock::share::service {
using InitFunc = std::function<int32_t()>;
using StartFunc = std::function<int32_t()>;
using ShutdownFunc = std::function<int32_t()>;
using ExitFunc = std::function<void()>;

struct ModuleDesc {
    std::string name;
    InitFunc init;
    StartFunc start;
    ShutdownFunc shutdown;
    ExitFunc exit;
    ModuleDesc() = default;
    ModuleDesc(std::string name, InitFunc initFunc, StartFunc startFunc, ShutdownFunc shutdownFunc, ExitFunc exitFunc)
        : name(std::move(name)),
          init(std::move(initFunc)),
          start(std::move(startFunc)),
          shutdown(std::move(shutdownFunc)),
          exit(std::move(exitFunc))
    {}
};

class MemShareService : public ock::common::Service {
public:
    MemShareService() noexcept;

    ~MemShareService() noexcept override
    {
        modules.clear();
    }

    static inline bool Inited() { return inited.load(); }

protected:
    HRESULT OnServiceProcessArgs(int argc, char *argv[]) override;

    HRESULT OnServiceInitialize() override
    {
        if (inited.load()) {
            return HOK;
        }
        for (auto it = modules.cbegin(); it != modules.cend(); ++it) {
            if (it->init == nullptr) {
                DBG_LOGINFO("[UBSMD]module ({}) no initialize function, skip.", it->name);
                continue;
            }
            DBG_LOGINFO("[UBSMD]module ({}) initialize begin...", it->name);
            auto ret = it->init();
            if (ret == 0) {
                DBG_LOGINFO("[UBSMD]module ({}) initialize success.", it->name);
            } else {
                DBG_LOGERROR("[UBSMD]module ({}) initialize failed({}).", it->name, ret);
                RollbackInit(it);
                return HFAIL;
            }
        }
        inited.store(true);
        return HOK;
    }

    HRESULT OnServiceStart() override
    {
        for (auto it = modules.cbegin(); it != modules.cend(); ++it) {
            if (it->start == nullptr) {
                DBG_LOGINFO("module ({}) no start function, skip.", it->name);
                continue;
            }

            DBG_LOGINFO("module ({}) start begin...", it->name);
            auto ret = it->start();
            if (ret == 0) {
                DBG_LOGINFO("module ({}) start success.", it->name);
            } else {
                DBG_LOGERROR("module ({}) start failed({}).", it->name, ret);
                RollbackStart(it);
                return HFAIL;
            }
        }

        return HOK;
    }

    HRESULT OnServiceHealthy() override
    {
        auto ret = HOK;
        return ret;
    }

    HRESULT OnServiceShutdown() override
    {
        for (auto it = modules.crbegin(); it != modules.crend(); ++it) {
            if (it->shutdown == nullptr) {
                DBG_LOGINFO("module ({}) no shutdown function, skip.", it->name);
                continue;
            }

            DBG_LOGINFO("module ({}) shutdown begin...", it->name);
            it->shutdown();
            DBG_LOGINFO("module ({}) shutdown finished", it->name);
        }
        return HOK;
    }

    HRESULT OnServiceUninitialize() override
    {
        for (auto it = modules.crbegin(); it != modules.crend(); ++it) {
            if (it->exit == nullptr) {
                DBG_LOGINFO("module ({}) no exit function, skip.", it->name);
                continue;
            }

            DBG_LOGINFO("module ({}) exit begin...", it->name);
            it->exit();
            DBG_LOGINFO("module ({}) exit finished", it->name);
        }

        return HOK;
    }

    void PrintVersionInfo() override {}

private:
    void RollbackInit(const std::vector<ModuleDesc>::const_iterator &end) noexcept
    {
        auto next = end;
        auto pos = end;
        for (--pos; next != modules.cbegin(); --next, --pos) {
            if (pos->exit != nullptr) {
                pos->exit();
            }
        }
    }

    void RollbackStart(const std::vector<ModuleDesc>::const_iterator &end) noexcept
    {
        auto next = end;
        auto pos = end;
        for (--pos; next != modules.cbegin(); --next, --pos) {
            if (pos->shutdown != nullptr) {
                pos->shutdown();
            }
        }
    }

private:
    std::vector<ModuleDesc> modules;
    static std::atomic_bool inited;
};
} // namespace ock::lease::service {


#endif // IOFWD_FORWARD_SERVICE_H
