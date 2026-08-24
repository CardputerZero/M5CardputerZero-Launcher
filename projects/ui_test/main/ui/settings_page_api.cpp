#include "settings_page.hpp"

#include <cstdio>
#include <mutex>

void adb_guide_api(int cmd, void *)
{
    if (cmd == SettingApiActivate) printf("ADB guide activate\n");
}

bool mork_api_read_flag = false;

namespace {

std::mutex mork_api_mutex;

}

void mork_api(int cmd, void *data)
{
    std::lock_guard<std::mutex> state_lock(mork_api_mutex);
    if (cmd == SettingApiReadFlag && data) {
        *static_cast<bool *>(data) = mork_api_read_flag;
    } else if (cmd == SettingApiReadFlagTimeStart && data) {
        auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
        std::get<0>(*result) = mork_api_read_flag;
    } else if (cmd == SettingApiActivate) {
        mork_api_read_flag = !mork_api_read_flag;
        printf("SettingApiActivate\n");
    }
}

#ifdef LAUNCHER_BUILD
SettingApiCallBackFunc launcher_app_setting_api(AppDescriptor desc)
{
    return [desc](int cmd, void *data) {
        if (cmd == SettingApiReadFlag && data) {
            *static_cast<bool *>(data) = launcher_app_registry_is_enabled(desc);
            return;
        }
        if (cmd == SettingApiReadFlagTimeStart && data) {
            auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
            std::get<0>(*result) = launcher_app_registry_is_enabled(desc);
            return;
        }
        if (cmd != SettingApiActivate) return;

        const bool enabled = launcher_app_registry_is_enabled(desc);
        if (launcher_app_registry_set_enabled(desc, !enabled))
            launcher_app_registry_notify_changed();
    };
}
#endif
