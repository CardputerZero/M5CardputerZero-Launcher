#include "cp0_lvgl_app.h"
#include "hal/hal_settings.h"
#include "hal_lvgl_bsp.h"

#include "../cp0_app_internal_utils.h"
#include "../cp0_settings_policy.hpp"
#include "../cp0_signal_registration.hpp"
#include "../cp0_sync_signal.hpp"
#include "sdl_bluetooth_settings.hpp"

#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <utility>

namespace {

class SettingsSystem
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = cp0::settings::Arguments;

    void api_call(const arg_t &args, const callback_t &callback)
    {
        try {
        if (!cp0::settings::valid_arguments(args)) {
            report(callback, -1, "invalid settings api request");
            return;
        }
        switch (cp0::settings::command_from(args)) {
        case cp0::settings::Command::BacklightRead: {
            const int value = hal_backlight_read();
            report(callback, value < 0 ? -1 : 0, std::to_string(value));
            break;
        }
        case cp0::settings::Command::BacklightMax: {
            const int value = hal_backlight_max();
            report(callback, value <= 0 ? -1 : 0, std::to_string(value));
            break;
        }
        case cp0::settings::Command::BacklightWrite: {
            int requested = 0;
            if (!cp0::settings::integer_argument(
                    args, 1, 0, std::numeric_limits<int>::max(), requested)) {
                report(callback, -1, "invalid settings api request");
                break;
            }
            const int value = hal_backlight_write(requested);
            report(callback, value < 0 ? -1 : 0, std::to_string(value));
            break;
        }
        case cp0::settings::Command::TimeStr: {
            char buffer[32] = {};
            hal_time_str(buffer, sizeof(buffer));
            report(callback, 0, buffer);
            break;
        }
        case cp0::settings::Command::GpioSet:
            report(callback, 0, "ok");
            break;
        case cp0::settings::Command::GpioGet:
            report(callback, 0, "0");
            break;
        case cp0::settings::Command::Log:
        case cp0::settings::Command::Unknown:
        default:
            report(callback, -1, "unknown settings api command");
            break;
        }
        } catch (...) {
            report(callback, -1, "settings api failure");
        }
    }

    static int api_int(const arg_t &args,
                       int default_value,
                       int minimum,
                       int maximum)
    {
        int result = default_value;
        cp0::signal::invoke_noexcept([&] { cp0_signal_settings_api(args, [&](int code, std::string data) {
            int parsed = 0;
            if (code == 0 &&
                cp0::settings::parse_integer_response(data, minimum, maximum, parsed))
                result = parsed;
        }); });
        return result;
    }

    static void api_time_str(char *buffer, int buffer_size)
    {
        if (!buffer || buffer_size <= 0) return;
        buffer[0] = '\0';
        cp0::signal::invoke_noexcept([&] { cp0_signal_settings_api({"TimeStr"}, [&](int code, std::string data) {
            if (code != 0) return;
            cp0_copy_string(buffer, buffer_size, data);
        }); });
        if (buffer[0] == '\0') hal_time_str(buffer, buffer_size);
    }

private:
    static void report(const callback_t &callback, int code, const std::string &data)
    {
        if (!callback) return;
        try {
            callback(code, data);
        } catch (...) {
        }
    }

};

} // namespace

extern "C" void init_settings(void)
{
    auto settings = std::make_shared<SettingsSystem>();
    static cp0::SignalRegistration<decltype(cp0_signal_settings_api)> registration;
    registration.replace(
        cp0_signal_settings_api,
        [settings](std::list<std::string> args,
                   std::function<void(int, std::string)> callback) {
            settings->api_call(args, callback);
        });
    sdl_bluetooth_settings::register_api();
}

extern "C" int cp0_backlight_read(void)
{
    return SettingsSystem::api_int(
        {"BacklightRead"}, -1, 0, std::numeric_limits<int>::max());
}

extern "C" int cp0_backlight_max(void)
{
    return SettingsSystem::api_int(
        {"BacklightMax"}, 100, 1, std::numeric_limits<int>::max());
}

extern "C" int cp0_backlight_write(int value)
{
    if (value < 0) return -1;
    return SettingsSystem::api_int(
        {"BacklightWrite", std::to_string(value)}, -1, 0,
        std::numeric_limits<int>::max());
}

extern "C" void cp0_time_str(char *buffer, int buffer_size)
{
    SettingsSystem::api_time_str(buffer, buffer_size);
}
