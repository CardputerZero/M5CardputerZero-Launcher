#include "settings_wifi_api.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace settings_wifi {
namespace {

constexpr std::size_t kMaxPasswordBytes = 64;

template <std::size_t Size>
std::string bounded_string(const char (&value)[Size])
{
    std::size_t length = 0;
    while (length < Size && value[length] != '\0') ++length;
    return std::string(value, length);
}

bool valid_text(const std::string &value, std::size_t max_bytes, bool allow_empty)
{
    if (!allow_empty && value.empty()) return false;
    if (value.size() >= max_bytes) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return byte >= 0x20u && byte != 0x7Fu;
    });
}

int validate_credentials(const std::string &ssid, const std::string &password)
{
    if (!valid_text(ssid, CP0_WIFI_SSID_MAX, false) ||
        !valid_text(password, kMaxPasswordBytes + 1, true))
        return CP0_WIFI_ERROR_INVALID;
    return 0;
}

}

bool is_open_security(const std::string &security)
{
    if (security.empty()) return true;
    std::string normalized = security;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char byte) {
        return static_cast<char>(std::toupper(byte));
    });
    return normalized == "OPEN" || normalized == "NONE" || normalized == "--";
}

int radio_enabled()
{
    return cp0_wifi_radio_enabled();
}

int radio_set_enabled(bool enabled)
{
    return cp0_wifi_radio_set_enabled(enabled ? 1 : 0);
}

int read_status(Status &status)
{
    status = {};
    cp0_wifi_status_t raw{};
    const int result = cp0_wifi_status_read(&raw);
    if (result != 0) return result;

    status.connected = raw.connected != 0;
    status.ssid = bounded_string(raw.ssid);
    status.ip = bounded_string(raw.ip);
    status.signal = std::clamp(raw.signal, 0, 100);
    status.ethernet = raw.ethernet != 0;
    return 0;
}

int scan(std::vector<AccessPoint> &access_points, int max_entries)
{
    access_points.clear();
    if (max_entries <= 0) return 0;

    const int limit = std::clamp(max_entries, 1, CP0_WIFI_AP_MAX);
    std::vector<cp0_wifi_ap_t> raw(static_cast<std::size_t>(limit));
    const int result = cp0_wifi_scan(raw.data(), limit);
    if (result < 0) return result;

    const int count = std::min(result, limit);
    access_points.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const cp0_wifi_ap_t &source = raw[static_cast<std::size_t>(index)];
        AccessPoint access_point;
        access_point.ssid = bounded_string(source.ssid);
        access_point.security = bounded_string(source.security);
        access_point.signal = std::clamp(source.signal, 0, 100);
        access_point.in_use = source.in_use != 0;
        access_point.saved = source.saved != 0;
        if (!access_point.ssid.empty()) access_points.push_back(std::move(access_point));
    }
    return static_cast<int>(access_points.size());
}

int connect(const std::string &ssid, const std::string &password)
{
    const int validation = validate_credentials(ssid, password);
    if (validation != 0) return validation;
    return cp0_wifi_connect(ssid.c_str(), password.empty() ? nullptr : password.c_str());
}

int connect_hidden(const std::string &ssid, const std::string &password)
{
    const int validation = validate_credentials(ssid, password);
    if (validation != 0) return validation;
    return cp0_wifi_connect_hidden(
        ssid.c_str(), password.empty() ? nullptr : password.c_str());
}

int profile_forget(const std::string &ssid)
{
    if (!valid_text(ssid, CP0_WIFI_SSID_MAX, false)) return CP0_WIFI_ERROR_INVALID;
    return cp0_wifi_profile_forget(ssid.c_str());
}

int profile_disconnect_active()
{
    return cp0_wifi_disconnect_active();
}

}
