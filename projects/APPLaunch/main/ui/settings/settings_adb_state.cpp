#include "settings_adb_state.hpp"

#include <charconv>
#include <limits>
#include <utility>

namespace setting {
namespace {

template <typename Callback>
void for_each_line(std::string_view text, Callback &&callback)
{
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t newline = text.find('\n', begin);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        std::string_view line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        callback(line);
        if (newline == std::string_view::npos) break;
        begin = newline + 1;
    }
}

bool is_hex(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

bool is_lower_hex(char value)
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

bool parse_nonnegative(std::string_view text, int &value)
{
    if (text.empty()) return false;
    int parsed = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < 0) return false;
    value = parsed;
    return true;
}

bool parse_authorization_line(std::string_view line, AdbAuthorization &authorization)
{
    static constexpr std::string_view prefix = "authorization=";
    if (line.size() <= prefix.size() + 65 || line.substr(0, prefix.size()) != prefix)
        return false;

    const std::string_view value = line.substr(prefix.size());
    const std::size_t tab = value.find('\t');
    if (tab != 64 || tab + 1 >= value.size()) return false;

    const std::string_view fingerprint = value.substr(0, tab);
    for (char byte : fingerprint) {
        if (!is_hex(byte)) return false;
    }
    for (unsigned char byte : value.substr(tab + 1)) {
        if (byte < 0x20U || byte == 0x7FU) return false;
    }
    authorization.fingerprint.assign(fingerprint.data(), fingerprint.size());
    authorization.label.assign(value.data() + tab + 1, value.size() - tab - 1);
    return true;
}

std::vector<AdbAuthorization> parse_authorizations_impl(std::string_view output,
                                                         bool *payload_valid)
{
    std::vector<AdbAuthorization> result;
    bool valid = true;
    for_each_line(output, [&](std::string_view line) {
        if (line.substr(0, std::string_view("authorization=").size()) != "authorization=")
            return;
        AdbAuthorization authorization;
        if (!parse_authorization_line(line, authorization)) {
            valid = false;
            return;
        }
        result.push_back(std::move(authorization));
    });
    if (payload_valid) *payload_valid = valid;
    return result;
}

} // namespace

AdbStatus parse_adb_status(std::string_view output)
{
    AdbStatus status;
    bool saw_adbd = false;
    bool saw_enabled = false;
    bool saw_authorization_count = false;
    bool payload_valid = true;
    bool active_value = false;
    bool enabled_value = false;

    for_each_line(output, [&](std::string_view line) {
        if (line == "adbd=active" || line == "adbd=inactive" || line == "adbd=failed") {
            const bool current_active = line == "adbd=active";
            if (saw_adbd && active_value != current_active) payload_valid = false;
            saw_adbd = true;
            active_value = current_active;
            status.active = current_active;
            return;
        }
        if (line == "enabled=enabled" || line == "enabled=disabled") {
            const bool current_enabled = line == "enabled=enabled";
            if (saw_enabled && enabled_value != current_enabled) payload_valid = false;
            saw_enabled = true;
            enabled_value = current_enabled;
            status.enabled = current_enabled;
            return;
        }

        static constexpr std::string_view count_prefix = "authorizations=";
        if (line.substr(0, count_prefix.size()) == count_prefix) {
            int count = 0;
            if (!parse_nonnegative(line.substr(count_prefix.size()), count)) {
                payload_valid = false;
                return;
            }
            if (saw_authorization_count && status.authorizations != count)
                payload_valid = false;
            saw_authorization_count = true;
            status.authorizations = count;
        }
    });

    bool authorization_payload_valid = true;
    status.authorization_entries = parse_authorizations_impl(output, &authorization_payload_valid);
    payload_valid = payload_valid && authorization_payload_valid;
    const std::size_t entry_count = status.authorization_entries.size();
    if (entry_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        payload_valid = false;
    } else if (!saw_authorization_count && !status.authorization_entries.empty()) {
        status.authorizations = static_cast<int>(entry_count);
    } else if (saw_authorization_count && !status.authorization_entries.empty() &&
               status.authorizations != static_cast<int>(entry_count)) {
        payload_valid = false;
    }

    status.valid = saw_adbd || saw_enabled;
    status.payload_valid = payload_valid;
    return status;
}

AdbStatus parse_adb_status(const char *output)
{
    return output ? parse_adb_status(std::string_view(output)) : AdbStatus{};
}

std::vector<AdbAuthorization> parse_adb_authorizations(std::string_view output)
{
    return parse_authorizations_impl(output, nullptr);
}

std::vector<AdbAuthorization> parse_adb_authorizations(const char *output)
{
    return output ? parse_adb_authorizations(std::string_view(output))
                  : std::vector<AdbAuthorization>{};
}

bool adb_public_key_valid(std::string_view key)
{
    if (key.size() < 702 || key.size() > 2048) return false;
    for (unsigned char byte : key) {
        if (byte < 0x20U || byte == 0x7FU) return false;
    }

    const std::size_t space = key.find(' ');
    if (space != 700 || space + 1 >= key.size() || key.substr(0, 5) != "QAAAA" ||
        key[space - 1] != '=')
        return false;
    for (std::size_t index = 0; index < space; ++index) {
        const unsigned char byte = static_cast<unsigned char>(key[index]);
        const bool base64 = (byte >= 'A' && byte <= 'Z') ||
                            (byte >= 'a' && byte <= 'z') ||
                            (byte >= '0' && byte <= '9') || byte == '+' ||
                            byte == '/' || byte == '=';
        if (!base64) return false;
    }
    return true;
}

bool adb_fingerprint_valid(std::string_view fingerprint)
{
    if (fingerprint.size() != 64) return false;
    for (char byte : fingerprint) {
        if (!is_lower_hex(byte)) return false;
    }
    return true;
}

bool adb_state_after_failure(const AdbStatus &status, bool previous)
{
    return status.valid && status.payload_valid ? status.enabled : previous;
}

PrivilegedResultKind classify_privileged_result(int result)
{
    switch (result) {
    case 0: return PrivilegedResultKind::SUCCESS;
    case 1: return PrivilegedResultKind::AUTH_FAILED;
    case 3: return PrivilegedResultKind::CANCELLED;
    case 4: return PrivilegedResultKind::TIMED_OUT;
    default: return PrivilegedResultKind::EXEC_FAILED;
    }
}

} // namespace setting
