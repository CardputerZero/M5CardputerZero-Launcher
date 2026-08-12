#pragma once

#include "cp0_lvgl_app.h"

#include <list>
#include <string>

namespace cp0::network {

enum class ApiCommand {
    Status,
    Scan,
    Connect,
    ConnectHidden,
    Disconnect,
    ProfileForget,
    ProfileExists,
    ProfileDisconnectActive,
    RadioEnabled,
    RadioSetEnabled,
};

struct ApiRequest
{
    ApiCommand command = ApiCommand::Status;
    int scan_limit = CP0_WIFI_AP_MAX;
    std::string ssid;
    std::string password;
    bool radio_enabled = false;
};

bool parse_api_request(const std::list<std::string> &args, ApiRequest &request);
const char *invalid_api_request_message();

std::string encode_status_payload(const cp0_wifi_status_t &status);
bool decode_status_payload(const std::string &payload, cp0_wifi_status_t &status);
std::string encode_scan_payload(const cp0_wifi_ap_t *access_points, int count);
int decode_scan_payload(
    const std::string &payload, cp0_wifi_ap_t *access_points, int capacity);

} // namespace cp0::network
