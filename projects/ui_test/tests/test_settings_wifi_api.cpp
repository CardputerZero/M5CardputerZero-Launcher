#include "settings_wifi_api.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace {

int radio_state = 1;
int radio_set_value = -1;
int status_result = 0;
int scan_result = 0;
int connect_result = 0;
int hidden_connect_result = 0;
int forget_result = 0;
int disconnect_result = 0;
int connect_calls = 0;
int hidden_connect_calls = 0;
int forget_calls = 0;
int disconnect_calls = 0;
std::string last_ssid;
std::string last_password;
cp0_wifi_status_t status_value{};
std::vector<cp0_wifi_ap_t> scan_values;

}

extern "C" int cp0_wifi_radio_enabled(void)
{
    return radio_state;
}

extern "C" int cp0_wifi_radio_set_enabled(int enabled)
{
    radio_set_value = enabled;
    return 0;
}

extern "C" int cp0_wifi_status_read(cp0_wifi_status_t *status)
{
    if (status) *status = status_value;
    return status_result;
}

extern "C" int cp0_wifi_scan(cp0_wifi_ap_t *entries, int max_entries)
{
    if (scan_result < 0) return scan_result;
    const int count = std::min<int>(scan_result, max_entries);
    for (int index = 0; index < count; ++index) entries[index] = scan_values[index];
    return scan_result;
}

extern "C" int cp0_wifi_connect(const char *ssid, const char *password)
{
    ++connect_calls;
    last_ssid = ssid ? ssid : "";
    last_password = password ? password : "";
    return connect_result;
}

extern "C" int cp0_wifi_connect_hidden(const char *ssid, const char *password)
{
    ++hidden_connect_calls;
    last_ssid = ssid ? ssid : "";
    last_password = password ? password : "";
    return hidden_connect_result;
}

extern "C" int cp0_wifi_profile_forget(const char *ssid)
{
    ++forget_calls;
    last_ssid = ssid ? ssid : "";
    return forget_result;
}

extern "C" int cp0_wifi_disconnect_active(void)
{
    ++disconnect_calls;
    return disconnect_result;
}

int main()
{
    assert(settings_wifi::is_open_security("") );
    assert(settings_wifi::is_open_security("Open"));
    assert(settings_wifi::is_open_security("NONE"));
    assert(!settings_wifi::is_open_security("WPA2"));

    assert(settings_wifi::radio_enabled() == 1);
    assert(settings_wifi::radio_set_enabled(false) == 0);
    assert(radio_set_value == 0);

    std::strcpy(status_value.ssid, "office");
    std::strcpy(status_value.ip, "192.168.1.9");
    status_value.connected = 1;
    status_value.signal = 87;
    settings_wifi::Status status;
    assert(settings_wifi::read_status(status) == 0);
    assert(status.connected && status.ssid == "office" && status.ip == "192.168.1.9");
    assert(status.signal == 87);

    status_result = -6;
    status.ssid = "stale";
    assert(settings_wifi::read_status(status) == -6);
    assert(!status.connected && status.ssid.empty() && status.ip.empty());
    status_result = 0;

    cp0_wifi_ap_t saved{};
    std::strcpy(saved.ssid, "office");
    std::strcpy(saved.security, "WPA2");
    saved.signal = 91;
    saved.in_use = 1;
    saved.saved = 1;
    cp0_wifi_ap_t open{};
    std::strcpy(open.ssid, "guest");
    std::strcpy(open.security, "OPEN");
    open.signal = 44;
    scan_values = {saved, open};
    scan_result = 2;
    std::vector<settings_wifi::AccessPoint> access_points;
    assert(settings_wifi::scan(access_points, 8) == 2);
    assert(access_points.size() == 2);
    assert(access_points[0].ssid == "office" && access_points[0].saved);
    assert(access_points[1].ssid == "guest" && !access_points[1].saved);

    scan_result = -2;
    assert(settings_wifi::scan(access_points, 8) == -2);
    assert(access_points.empty());
    scan_result = 2;

    connect_result = -7;
    assert(settings_wifi::connect("office", "secret") == -7);
    assert(connect_calls == 1 && last_ssid == "office" && last_password == "secret");
    hidden_connect_result = 0;
    assert(settings_wifi::connect_hidden("hidden", "") == 0);
    assert(hidden_connect_calls == 1 && last_ssid == "hidden" && last_password.empty());
    assert(settings_wifi::connect("bad\nssid", "secret") == CP0_WIFI_ERROR_INVALID);
    assert(connect_calls == 1);
    assert(settings_wifi::connect("", "secret") == CP0_WIFI_ERROR_INVALID);
    assert(settings_wifi::connect("office", std::string(65, 'x')) == CP0_WIFI_ERROR_INVALID);
    assert(connect_calls == 1);

    forget_result = 0;
    assert(settings_wifi::profile_forget("office") == 0);
    assert(forget_calls == 1 && last_ssid == "office");
    disconnect_result = -1;
    assert(settings_wifi::profile_disconnect_active() == -1);
    assert(disconnect_calls == 1);
    return 0;
}
