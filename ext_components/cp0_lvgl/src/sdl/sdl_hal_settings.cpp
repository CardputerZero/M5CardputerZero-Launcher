#include "cp0_lvgl_app.h"
#include "hal/hal_settings.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" time_t cp0_sdl_time_now(void);

namespace {

std::atomic_bool simulated_bt_powered{false};

} // namespace

extern "C" int hal_backlight_read(void) { return 75; }
extern "C" int hal_backlight_max(void) { return 100; }

extern "C" int hal_backlight_write(int value)
{
    return std::max(0, std::min(hal_backlight_max(), value));
}

extern "C" hal_battery_info_t hal_battery_read(void)
{
    const cp0_battery_info_t source = cp0_battery_read();
    hal_battery_info_t result{};
    result.voltage_mv = source.voltage_mv;
    result.current_ma = source.current_ma;
    result.temperature_c10 = source.temperature_c10;
    result.soc = source.soc;
    result.remain_mah = source.remain_mah;
    result.full_mah = source.full_mah;
    result.flags = source.flags;
    result.avg_current_ma = source.avg_current_ma;
    result.valid = source.valid;
    return result;
}

extern "C" int hal_volume_read(void) { return 39; }
extern "C" int hal_volume_write(int value) { return std::max(0, std::min(63, value)); }

extern "C" hal_wifi_status_t hal_wifi_get_status(void)
{
    hal_wifi_status_t status{};
    status.connected = 1;
    std::strncpy(status.ssid, "SimulatedWiFi", WIFI_SSID_MAX - 1);
    std::strncpy(status.ip, "192.168.1.100", sizeof(status.ip) - 1);
    status.signal = 80;
    return status;
}

extern "C" int hal_wifi_scan(hal_wifi_ap_t *output, int max_access_points)
{
    if (!output || max_access_points <= 0) return 0;

    const int count = std::min(max_access_points, 3);
    std::memset(output, 0, sizeof(hal_wifi_ap_t) * static_cast<size_t>(count));
    if (count > 0) {
        std::strncpy(output[0].ssid, "SimulatedWiFi", WIFI_SSID_MAX - 1);
        std::strncpy(output[0].security, "WPA2", sizeof(output[0].security) - 1);
        output[0].signal = 80;
        output[0].in_use = 1;
    }
    if (count > 1) {
        std::strncpy(output[1].ssid, "Neighbor_5G", WIFI_SSID_MAX - 1);
        std::strncpy(output[1].security, "WPA2", sizeof(output[1].security) - 1);
        output[1].signal = 55;
    }
    if (count > 2) {
        std::strncpy(output[2].ssid, "FreeWiFi", WIFI_SSID_MAX - 1);
        std::strncpy(output[2].security, "Open", sizeof(output[2].security) - 1);
        output[2].signal = 30;
    }
    return count;
}

extern "C" int hal_wifi_connect(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
    return 0;
}

extern "C" int hal_wifi_disconnect(void) { return 0; }

extern "C" hal_bt_status_t hal_bt_get_status(void)
{
    hal_bt_status_t status{};
    status.powered = simulated_bt_powered.load(std::memory_order_acquire) ? 1 : 0;
    std::strncpy(status.address, "00:00:00:00:00:00", sizeof(status.address) - 1);
    std::strncpy(status.alias, "CardputerZero", sizeof(status.alias) - 1);
    return status;
}

extern "C" int hal_bt_set_power(int on)
{
    simulated_bt_powered.store(on != 0, std::memory_order_release);
    return 0;
}

extern "C" int hal_bt_scan(hal_bt_device_t *output, int max_devices)
{
    (void)output;
    (void)max_devices;
    return 0;
}

extern "C" void hal_time_str(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) return;
    const std::time_t now = cp0_sdl_time_now();
    const std::tm *time = std::localtime(&now);
    if (!time) {
        buffer[0] = '\0';
        return;
    }
    std::snprintf(buffer, static_cast<size_t>(buffer_size), "%02d:%02d", time->tm_hour,
                  time->tm_min);
}
