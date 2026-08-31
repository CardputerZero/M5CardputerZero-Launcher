#include "settings_extport.hpp"

#include "hal_lvgl_bsp.h"
#include "settings_tree_types.hpp"

#include <tuple>

void ext_port_com(const std::string &port, int cmd, void *data)
{
    bool value = false;
    if (cmd == SettingApiReadFlag && data) value = *static_cast<bool *>(data);
    if (cmd == SettingApiReadFlagTimeStart && data)
        value = std::get<0>(*static_cast<SettingApiReadFlagTimeStartData *>(data));

    if (cmd == SettingApiReadFlag || cmd == SettingApiReadFlagTimeStart) {
        bool valid = false;
        try {
            cp0_signal_settings_api({"GpioGet", port}, [&value, &valid](int code, std::string response) {
                if (code == 0 && (response == "0" || response == "1")) {
                    value = response == "1";
                    valid = true;
                }
            });
        } catch (...) {
        }
        if (cmd == SettingApiReadFlag && data && valid) *static_cast<bool *>(data) = value;
        if (cmd == SettingApiReadFlagTimeStart && data) {
            auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
            if (valid) std::get<0>(*result) = value;
            if (std::get<1>(*result)) std::get<1>(*result)->store(false);
        }
        return;
    }

    if (cmd != SettingApiActivate) return;
    bool current = false;
    bool valid   = false;
    try {
        cp0_signal_settings_api({"GpioGet", port}, [&current, &valid](int code, std::string response) {
            if (code == 0 && (response == "0" || response == "1")) {
                current = response == "1";
                valid   = true;
            }
        });
        if (valid) cp0_signal_settings_api({"GpioSet", port, current ? "0" : "1"}, nullptr);
    } catch (...) {
    }
}
