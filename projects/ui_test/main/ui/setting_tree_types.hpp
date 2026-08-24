/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "eventpp/callbacklist.h"
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
    PageType page_type = PageType::NextPageNeeded;

    SettingEntry() = default;
    SettingEntry(const std::string &name) : label(name)
    {
    }
    SettingEntry(const std::string &name, SettingApiCallBack callback) : label(name), Componens_api(std::move(callback))
    {
    }
    SettingEntry(const std::string &name, SettingApiCallBack callback, bool enable_icon)
        : label(name), Componens_api(std::move(callback)), icon_enabled(enable_icon)
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
};
