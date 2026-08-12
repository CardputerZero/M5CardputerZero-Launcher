#include "hal_lvgl_bsp.h"
#include "commount.h"
#include "cp0_lvgl_app.h"
#include "cp0_network_api_contract.hpp"
#include "cp0_integer_codec.hpp"
#include "cp0_sync_signal.hpp"
#include "lvgl/lvgl.h"
#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <string>
#include <cstdio>

#define def_hal_fun(arg, name) eventpp::CallbackList<arg> name;
#include "signal_register_plan.h"
#undef def_hal_fun

eventpp::EventQueue<CP0_C_EVENT_t, void(const std::list<std::string>)> cp0_task_queue;

extern "C" int cp0_wifi_status_read(cp0_wifi_status_t *status)
{
    if (!status)
        return -1;

    *status = {};
    int result = -1;
    cp0::signal::invoke_noexcept([&] { cp0_signal_wifi_api({"Status"}, [&](int code, std::string data) {
        if (code != 0 || !cp0::network::decode_status_payload(data, *status)) {
            result = code != 0 ? code : -1;
            return;
        }
        result = 0;
    }); });
    return result;
}

namespace {

int wifi_command(const std::list<std::string> &args)
{
    int result = -1;
    cp0::signal::invoke_noexcept([&] {
        cp0_signal_wifi_api(args, [&](int code, std::string) { result = code; });
    });
    return result;
}

} // namespace

extern "C" int cp0_wifi_scan(cp0_wifi_ap_t *entries, int max_entries)
{
    if (!entries || max_entries <= 0)
        return 0;
    std::fill_n(entries, static_cast<size_t>(max_entries), cp0_wifi_ap_t{});
    const int request_limit = std::min(max_entries, CP0_WIFI_AP_MAX);
    int count = CP0_WIFI_ERROR_SERVICE;
    cp0::signal::invoke_noexcept([&] { cp0_signal_wifi_api({"Scan", std::to_string(request_limit)}, [&](int code, std::string data) {
        if (code < 0) {
            count = code;
            return;
        }
        count = cp0::network::decode_scan_payload(data, entries, max_entries);
    }); });
    return count;
}

extern "C" int cp0_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0])
        return -1;
    return password && password[0] ? wifi_command({"Connect", ssid, password})
                                   : wifi_command({"Connect", ssid});
}

extern "C" int cp0_wifi_connect_hidden(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0])
        return -1;
    return password && password[0] ? wifi_command({"ConnectHidden", ssid, password})
                                   : wifi_command({"ConnectHidden", ssid});
}

extern "C" int cp0_wifi_profile_forget(const char *ssid)
{
    return ssid && ssid[0] ? wifi_command({"ProfileForget", ssid}) : -1;
}

extern "C" int cp0_wifi_profile_exists(const char *ssid)
{
    return ssid && ssid[0] ? wifi_command({"ProfileExists", ssid}) : 0;
}

extern "C" int cp0_wifi_disconnect_active(void)
{
    return wifi_command({"ProfileDisconnectActive"});
}

extern "C" int cp0_wifi_radio_enabled(void)
{
    return wifi_command({"RadioEnabled"});
}

extern "C" int cp0_wifi_radio_set_enabled(int enabled)
{
    return wifi_command({"RadioSetEnabled", enabled ? "on" : "off"});
}
static std::atomic_bool queue_run_flage{true};
extern "C" void init_lvgl_event_cpp()
{
    // cp0_task_queue.appendListener(CP0_C_EVENT_END, [](const std::list<std::string> args)
    //                               { queue_run_flage = false; });
    // std::thread t([]()
    //               {
    //     while (queue_run_flage.load()) {
    //         cp0_task_queue.wait();
    //         cp0_task_queue.process();
    //     } });
    // t.detach();
}

static int config_get_int(const char *key, int default_val)
{
    int val = default_val;
    cp0::signal::invoke_noexcept([&] { cp0_signal_config_api({"GetInt", key ? std::string(key) : std::string(), std::to_string(default_val)},
                          [&](int code, std::string data) {
                              int parsed = 0;
                              if (code == 0 && cp0_integer_codec::parse_int(data, parsed))
                                  val = parsed;
                          }); });
    return val;
}

static void saved_volume_write(int val)
{
    cp0::signal::invoke_noexcept([&] {
        cp0_signal_audio_api({"VolumeWrite", std::to_string(val)}, nullptr);
    });
}

static void saved_gpio_write(const char *name)
{
    const int value = config_get_int(name, -1);
    if (value != 0 && value != 1)
        return;
    cp0::signal::invoke_noexcept([&] {
        cp0_signal_settings_api({"GpioSet", name, std::to_string(value)}, nullptr);
    });
}

extern "C" void init_lvgl_saved_settings()
{
    int saved_bright = config_get_int("brightness", -1);
    if (saved_bright > 0)
        cp0_backlight_write(saved_bright);

    int saved_vol = config_get_int("volume", -1);
    if (saved_vol >= 0)
        saved_volume_write(saved_vol);

    saved_gpio_write("extport_usb");
    saved_gpio_write("extport_5vout");
}
