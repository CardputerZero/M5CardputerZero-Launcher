#pragma once

#include "cp0_lvgl_app.h"

#include <string>
#include <vector>

namespace settings_wifi {

struct AccessPoint {
    std::string ssid;
    std::string security;
    int signal = 0;
    bool in_use = false;
    bool saved = false;
};

struct Status {
    bool connected = false;
    std::string ssid;
    std::string ip;
    int signal = 0;
    bool ethernet = false;
};

bool is_open_security(const std::string &security);

int radio_enabled();
int radio_set_enabled(bool enabled);
int read_status(Status &status);
int scan(std::vector<AccessPoint> &access_points,
         int max_entries = CP0_WIFI_AP_MAX);
int connect(const std::string &ssid, const std::string &password);
int connect_hidden(const std::string &ssid, const std::string &password);
int profile_forget(const std::string &ssid);
int profile_disconnect_active();

}
