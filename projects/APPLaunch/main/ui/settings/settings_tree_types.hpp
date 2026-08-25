/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

#include "eventpp/callbacklist.h"
#include "settings_async_dispatch.hpp"
#include "tree.hh"

struct _lv_obj_t;

namespace DComponens {
class LvglComponensBase;
}

using SettingApiCallBack              = eventpp::CallbackList<void(int, void *)>;
using SettingApiCallBackFunc          = std::function<void(int, void *)>;
using SettingApiReadFlagTimeStartData = std::tuple<bool, std::atomic_bool *>;
enum SettingApiEvent : int {
    SettingApiReadFlag          = 0,
    SettingApiActivate          = 1,
    SettingApiReadFlagTimeStart = 2,
};

enum class SettingApiResult : uint8_t {
    NotHandled,
    Success,
    Pending,
    Failure,
    Cancelled,
};

enum class SettingComponentState : uint8_t {
    Read,
    Activate,
    Pending,
    Success,
    Failure,
    Cancelled,
};

enum class SettingActivationPolicy : uint8_t {
    LeaveImmediately,
    WaitForResult,
};

enum class SettingStatusReadPolicy : uint8_t {
    Async,
    Direct,
};

using SettingApiAsyncCallBackFunc = std::function<SettingApiResult(int, void *)>;

class SettingRequestState {
public:
    uint64_t begin_activation() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == SettingComponentState::Activate || state_ == SettingComponentState::Pending) return 0;
        state_ = SettingComponentState::Activate;
        ++generation_;
        if (generation_ == 0) generation_ = 1;
        return generation_;
    }

    bool mark_pending(uint64_t generation) noexcept
    {
        return transition(generation, SettingComponentState::Pending);
    }

    bool mark_success(uint64_t generation) noexcept
    {
        return transition(generation, SettingComponentState::Success);
    }

    bool mark_failure(uint64_t generation) noexcept
    {
        return transition(generation, SettingComponentState::Failure);
    }

    bool mark_cancelled(uint64_t generation) noexcept
    {
        return transition(generation, SettingComponentState::Cancelled);
    }

    void mark_read() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = SettingComponentState::Read;
    }

    SettingComponentState state() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    uint64_t generation() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_;
    }

    bool is_current(uint64_t generation) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation != 0 && generation == generation_;
    }

    bool pending() const noexcept
    {
        const SettingComponentState current = state();
        return current == SettingComponentState::Activate || current == SettingComponentState::Pending;
    }

private:
    bool transition(uint64_t generation, SettingComponentState next) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == 0 || generation != generation_) return false;
        if (next == SettingComponentState::Pending) {
            if (state_ != SettingComponentState::Activate) return false;
            state_ = next;
            return true;
        }
        if (state_ != SettingComponentState::Activate && state_ != SettingComponentState::Pending) return false;
        state_ = next;
        return true;
    }

    mutable std::mutex mutex_;
    SettingComponentState state_ = SettingComponentState::Read;
    uint64_t generation_         = 1;
};

struct SettingEntry;
using Tree     = tree<SettingEntry>;
using NodeIter = Tree::iterator;

using SettingPageFactory =
    std::function<std::unique_ptr<DComponens::LvglComponensBase>(_lv_obj_t *, const NodeIter &, std::function<void()>)>;
enum class PageType : int {
    Normal         = 0,
    NextPageNeeded = 1,
    FullCustom     = 2,
};
struct SettingEntry {
    std::string label;
    SettingPageFactory page_factory;
    bool icon_enabled = false;
    SettingApiCallBack Componens_api;
    SettingApiAsyncCallBackFunc Async_api;
    SettingActivationPolicy activation_policy = SettingActivationPolicy::LeaveImmediately;
    SettingStatusReadPolicy status_read_policy = SettingStatusReadPolicy::Async;
    std::shared_ptr<SettingRequestState> request_state = std::make_shared<SettingRequestState>();
    PageType page_type = PageType::NextPageNeeded;

    SettingEntry() = default;
    SettingEntry(const std::string &name) : label(name)
    {
    }
    SettingEntry(const std::string &name, SettingApiCallBack callback) : label(name), Componens_api(std::move(callback))
    {
    }
    SettingEntry(const std::string &name, SettingApiCallBack callback, bool enable_icon)
        : label(name), icon_enabled(enable_icon), Componens_api(std::move(callback))
    {
    }
    SettingEntry(const std::string &name, SettingApiCallBackFunc func, bool enable_icon)
        : label(name), icon_enabled(enable_icon)
    {
        Componens_api.append(std::move(func));
    }

    SettingEntry(const std::string &name, SettingPageFactory factory) : label(name), page_factory(std::move(factory))
    {
    }
    SettingEntry(const std::string &name, SettingPageFactory factory, PageType type)
        : label(name), page_factory(std::move(factory)), page_type(type)
    {
    }
    SettingEntry(const std::string &name,
                 SettingPageFactory factory,
                 SettingApiCallBackFunc func,
                 PageType type)
        : label(name), page_factory(std::move(factory)), page_type(type)
    {
        Componens_api.append(std::move(func));
    }
    SettingEntry(const std::string &name, SettingPageFactory factory, SettingApiCallBackFunc func)
        : label(name), page_factory(std::move(factory))
    {
        Componens_api.append(std::move(func));
    }
    SettingEntry(const std::string &name, SettingPageFactory factory, SettingApiCallBackFunc func, bool enable_icon)
        : label(name), page_factory(std::move(factory)), icon_enabled(enable_icon)
    {
        Componens_api.append(std::move(func));
    }

    void set_async_api(SettingApiAsyncCallBackFunc func,
                       SettingActivationPolicy policy = SettingActivationPolicy::WaitForResult)
    {
        Async_api          = std::move(func);
        activation_policy = policy;
    }

    static SettingEntry make_async(const std::string &name,
                                   SettingApiAsyncCallBackFunc func,
                                   bool enable_icon = false,
                                   SettingActivationPolicy policy = SettingActivationPolicy::WaitForResult)
    {
        SettingEntry entry{name};
        entry.icon_enabled = enable_icon;
        entry.set_async_api(std::move(func), policy);
        return entry;
    }

    bool has_api() const noexcept
    {
        return static_cast<bool>(Componens_api) || static_cast<bool>(Async_api);
    }
};
