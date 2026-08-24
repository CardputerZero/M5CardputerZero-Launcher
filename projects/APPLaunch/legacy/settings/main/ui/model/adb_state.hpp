#pragma once

#include "cp0_lvgl_app.h"
#include <string>
#include <vector>

namespace setting {

struct AdbStatus {
    bool valid = false;
    bool active = false;
    bool enabled = false;
    int authorizations = 0;
};

struct AdbAuthorization {
    std::string fingerprint;
    std::string label;
};

AdbStatus parse_adb_status(const char *output);
bool adb_state_after_failure(const AdbStatus &status, bool previous);
bool adb_public_key_valid(const std::string &key);
std::vector<AdbAuthorization> parse_adb_authorizations(const char *output);

} // namespace setting
