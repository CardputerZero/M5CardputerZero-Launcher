#include "settings_page.hpp"

#include "settings_adb_api.hpp"

#include <mutex>
#include <tuple>

namespace {

std::mutex adb_api_mutex;
settings_adb::AdbApi adb_api;
bool adb_enabled = false;
bool adb_initialized = false;
bool adb_pending = false;

void refresh_adb_status()
{
    if (adb_pending || adb_initialized) return;
    adb_pending = true;
    try {
        adb_api.query_status([](settings_adb::Result result) {
            std::lock_guard<std::mutex> lock(adb_api_mutex);
            adb_pending = false;
            if (result.ok() && result.status_valid) {
                adb_enabled = result.status.enabled;
                adb_initialized = true;
            }
        });
    } catch (...) {
        adb_pending = false;
    }
}

} // namespace

void adb_guide_api(int, void *)
{
}

void adb_toggle_api(int cmd, void *data)
{
    if (cmd == SettingApiReadFlag && data) {
        {
            std::lock_guard<std::mutex> lock(adb_api_mutex);
            refresh_adb_status();
        }
        *static_cast<bool *>(data) = adb_enabled;
        return;
    }
    if (cmd == SettingApiReadFlagTimeStart && data) {
        auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
        {
            std::lock_guard<std::mutex> lock(adb_api_mutex);
            refresh_adb_status();
            std::get<0>(*result) = adb_enabled;
            if (std::get<1>(*result)) std::get<1>(*result)->store(adb_pending);
        }
        return;
    }
    if (cmd != SettingApiActivate) return;

    bool desired = false;
    {
        std::lock_guard<std::mutex> lock(adb_api_mutex);
        if (adb_pending) return;
        desired = !adb_enabled;
        adb_pending = true;
    }
    try {
        adb_api.set_enabled(desired, [desired](settings_adb::Result result) {
            std::lock_guard<std::mutex> callback_lock(adb_api_mutex);
            adb_pending = false;
            if (result.ok()) {
                adb_enabled = desired;
                adb_initialized = true;
            }
        });
    } catch (...) {
        adb_pending = false;
    }
}
