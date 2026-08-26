#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

#include "cp0_lvgl_app.h"
#include "setting_tree_types.hpp"
#include "setting_wifi_hidden_page3.hpp"
#include "setting_wifi_scan_page3.hpp"

// Wi-Fi-specific state stays outside the tree description.  The tree only
// binds its Power entry and page factories to this small adapter.
namespace cp0_ui_wifi {

static bool power_state = false;
static bool power_pending = false;
static std::recursive_mutex state_mutex;

static bool query_power(bool &enabled)
{
    int result = -1;
    try {
        cp0_signal_wifi_api({"RadioEnabled"}, [&](int code, std::string) {
            result = code;
        });
    } catch (...) {
        return false;
    }
    if (result != 0 && result != 1) return false;
    enabled = result == 1;
    return true;
}

static void power_api(int cmd, void *data)
{
    if (cmd == SettingApiReadFlag && data) {
        {
            std::lock_guard<std::recursive_mutex> lock(state_mutex);
            if (power_pending) {
                *static_cast<bool *>(data) = power_state;
                return;
            }
        }
        bool enabled = false;
        const bool success = query_power(enabled);
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        if (!power_pending && success) power_state = enabled;
        *static_cast<bool *>(data) = power_state;
    } else if (cmd == SettingApiReadFlagTimeStart && data) {
        auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
        {
            std::lock_guard<std::recursive_mutex> lock(state_mutex);
            if (power_pending) {
                std::get<0>(*result) = power_state;
                return;
            }
        }
        bool enabled = false;
        const bool success = query_power(enabled);
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        if (!power_pending && success) power_state = enabled;
        std::get<0>(*result) = power_state;
    } else if (cmd == SettingApiActivate) {
        bool next = false;
        {
            std::lock_guard<std::recursive_mutex> lock(state_mutex);
            if (power_pending) return;
            next = !power_state;
            power_state = next;
            power_pending = true;
        }
        try {
            cp0_signal_wifi_api({"RadioSetEnabled", next ? "on" : "off"},
                                [next](int code, std::string) {
                                    std::lock_guard<std::recursive_mutex> callback_lock(state_mutex);
                                    power_pending = false;
                                    if (code < 0) power_state = !next;
                                });
        } catch (...) {
            std::lock_guard<std::recursive_mutex> lock(state_mutex);
            power_pending = false;
            power_state = !next;
        }
    }
}

static std::unique_ptr<DComponens::LvglComponensBase> scan_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back)
{
    bool enabled = false;
    query_power(enabled);
    return std::make_unique<LvSettingWifiScanPage3>(
        parent, page_node, std::move(on_back), enabled);
}

static std::unique_ptr<DComponens::LvglComponensBase> hidden_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back)
{
    bool enabled = false;
    query_power(enabled);
    return std::make_unique<LvSettingWifiHiddenPage3>(
        parent, page_node, std::move(on_back), enabled);
}

} // namespace cp0_ui_wifi
