#pragma once

#include "cp0_lvgl_app.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace cp0::wifi {

inline int classify_command_failure(const std::string &output)
{
    std::string lower = output;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lower.find("secrets") != std::string::npos ||
        lower.find("password") != std::string::npos ||
        lower.find("authentication") != std::string::npos)
        return CP0_WIFI_ERROR_AUTH;
    if (lower.find("not found") != std::string::npos ||
        lower.find("no network") != std::string::npos)
        return CP0_WIFI_ERROR_NOT_FOUND;
    if (lower.find("ip config") != std::string::npos ||
        lower.find("dhcp") != std::string::npos)
        return CP0_WIFI_ERROR_IP_CONFIG;
    if (lower.find("radio") != std::string::npos ||
        lower.find("disabled") != std::string::npos)
        return CP0_WIFI_ERROR_RADIO_OFF;
    return CP0_WIFI_ERROR_SERVICE;
}

} // namespace cp0::wifi
