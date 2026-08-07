#include "adb_state.hpp"
#include <charconv>
#include <cstring>
#include <cctype>

namespace setting {
namespace {
bool has_line(const char *output, const char *line)
{
    if (!output || !line) return false;
    const std::size_t length = std::strlen(line);
    for (const char *p = output; (p = std::strstr(p, line)) != nullptr; ++p) {
        const bool start = p == output || p[-1] == '\n' || p[-1] == '\r';
        const char next = p[length];
        const bool end = next == '\0' || next == '\n' || next == '\r';
        if (start && end) return true;
    }
    return false;
}

bool parse_nonnegative_line_value(const char *output, const char *prefix, int &value)
{
    if (!output || !prefix) return false;
    const std::size_t prefix_length = std::strlen(prefix);
    const char *line = output;
    while (*line) {
        const char *end = line + std::strcspn(line, "\r\n");
        if (static_cast<std::size_t>(end - line) > prefix_length &&
            std::strncmp(line, prefix, prefix_length) == 0) {
            int parsed = 0;
            const char *first = line + prefix_length;
            const auto result = std::from_chars(first, end, parsed);
            if (result.ec == std::errc{} && result.ptr == end && parsed >= 0) {
                value = parsed;
                return true;
            }
        }
        line = end;
        while (*line == '\r' || *line == '\n') ++line;
    }
    return false;
}
} // namespace

AdbStatus parse_adb_status(const char *output)
{
    AdbStatus status;
    if (!output) return status;
    status.active = has_line(output, "adbd=active");
    status.enabled = has_line(output, "enabled=enabled");
    (void)parse_nonnegative_line_value(output, "authorizations=", status.authorizations);
    status.valid = status.active || status.enabled || has_line(output, "adbd=inactive") ||
                   has_line(output, "adbd=failed") || has_line(output, "enabled=disabled");
    return status;
}

bool adb_public_key_valid(const std::string &key)
{
    if (key.size() < 702 || key.size() > 2048 || key.find_first_of("\r\n") != std::string::npos)
        return false;
    const std::size_t space = key.find(' ');
    if (space != 700 || space + 1 == key.size() || key.compare(0, 5, "QAAAA") != 0 ||
        key[space - 1] != '=')
        return false;
    for (std::size_t i = 0; i < space; ++i) {
        const unsigned char c = static_cast<unsigned char>(key[i]);
        if (!std::isalnum(c) && c != '+' && c != '/' && c != '=') return false;
    }
    return true;
}

std::vector<AdbAuthorization> parse_adb_authorizations(const char *output)
{
    std::vector<AdbAuthorization> result;
    if (!output) return result;
    const char *line = output;
    while (*line) {
        const char *end = std::strchr(line, '\n');
        if (!end) end = line + std::strlen(line);
        static constexpr char prefix[] = "authorization=";
        if (static_cast<std::size_t>(end - line) > sizeof(prefix) - 1 + 65 &&
            std::strncmp(line, prefix, sizeof(prefix) - 1) == 0) {
            const char *fingerprint = line + sizeof(prefix) - 1;
            const char *tab = static_cast<const char *>(std::memchr(fingerprint, '\t', end - fingerprint));
            if (tab && tab - fingerprint == 64) {
                bool hex = true;
                for (const char *p = fingerprint; p < tab; ++p)
                    hex = hex && std::isxdigit(static_cast<unsigned char>(*p));
                if (hex && tab + 1 < end)
                    result.push_back({std::string(fingerprint, tab), std::string(tab + 1, end)});
            }
        }
        line = *end ? end + 1 : end;
    }
    return result;
}

bool adb_state_after_failure(const AdbStatus &status, bool previous)
{
    return status.valid ? status.enabled : previous;
}
} // namespace setting
