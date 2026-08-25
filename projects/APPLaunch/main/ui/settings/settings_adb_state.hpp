#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace setting {

struct AdbAuthorization {
    std::string fingerprint;
    std::string label;
};

struct AdbStatus {
    bool valid = false;
    bool active = false;
    bool enabled = false;
    int authorizations = 0;
    std::vector<AdbAuthorization> authorization_entries;
    bool payload_valid = true;
};

enum class PrivilegedResultKind : uint8_t {
    SUCCESS,
    AUTH_FAILED,
    EXEC_FAILED,
    CANCELLED,
    TIMED_OUT,
};

AdbStatus parse_adb_status(std::string_view output);
AdbStatus parse_adb_status(const char *output);

std::vector<AdbAuthorization> parse_adb_authorizations(std::string_view output);
std::vector<AdbAuthorization> parse_adb_authorizations(const char *output);

bool adb_public_key_valid(std::string_view key);
bool adb_fingerprint_valid(std::string_view fingerprint);

bool adb_state_after_failure(const AdbStatus &status, bool previous);
PrivilegedResultKind classify_privileged_result(int result);

} // namespace setting
