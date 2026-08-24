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

bool start_adb_status_refresh()
{
    {
        std::lock_guard<std::mutex> lock(adb_api_mutex);
        if (adb_pending || adb_initialized) return false;
        adb_pending = true;
    }

    bool started = false;
    try {
        started = adb_api.query_status([](settings_adb::Result result) {
            std::lock_guard<std::mutex> lock(adb_api_mutex);
            adb_pending = false;
            if (result.ok() && result.status_valid) {
                adb_enabled = result.status.enabled;
                adb_initialized = true;
            }
        });
    } catch (...) {
        started = false;
    }

    if (!started) {
        std::lock_guard<std::mutex> lock(adb_api_mutex);
        adb_pending = false;
    }
    return started;
}

} // namespace

void adb_guide_api(int, void *)
{
}

void adb_toggle_api(int cmd, void *data)
{
    if (cmd == SettingApiReadFlag && data) {
        bool should_refresh = false;
        {
            std::lock_guard<std::mutex> lock(adb_api_mutex);
            should_refresh = !adb_pending && !adb_initialized;
        }
        if (should_refresh) start_adb_status_refresh();
        std::lock_guard<std::mutex> lock(adb_api_mutex);
        *static_cast<bool *>(data) = adb_enabled;
        return;
    }
    if (cmd == SettingApiReadFlagTimeStart && data) {
        auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
        bool pending = false;
        {
            std::lock_guard<std::mutex> lock(adb_api_mutex);
            std::get<0>(*result) = adb_enabled;
            pending = adb_pending;
        }
        if (!pending) pending = start_adb_status_refresh();
        if (std::get<1>(*result)) std::get<1>(*result)->store(pending);
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
    bool started = false;
    try {
        started = adb_api.set_enabled(desired, [desired](settings_adb::Result result) {
            std::lock_guard<std::mutex> callback_lock(adb_api_mutex);
            adb_pending = false;
            if (result.ok()) {
                adb_enabled = desired;
                adb_initialized = true;
            }
        });
    } catch (...) {
        started = false;
    }
    if (!started) {
        std::lock_guard<std::mutex> lock(adb_api_mutex);
        adb_pending = false;
    }
}
