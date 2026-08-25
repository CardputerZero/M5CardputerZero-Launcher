#include "cp0_network_api_contract.hpp"

#include <charconv>
#include <cstdio>
#include <limits>
#include <string_view>
#include <vector>

namespace cp0::network {
namespace {

bool parse_integer(std::string_view text, int minimum, int maximum, int &value)
{
    if (text.empty())
        return false;
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed < minimum || parsed > maximum)
        return false;
    value = parsed;
    return true;
}

std::string escape_field(std::string_view field)
{
    std::string encoded;
    encoded.reserve(field.size());
    for (const char character : field) {
        switch (character) {
        case '\\': encoded += "\\\\"; break;
        case ':': encoded += "\\:"; break;
        case '\n': encoded += "\\n"; break;
        case '\r': encoded += "\\r"; break;
        default: encoded += character; break;
        }
    }
    return encoded;
}

bool split_record(std::string_view record, std::vector<std::string> &fields)
{
    fields.clear();
    std::string field;
    for (std::size_t index = 0; index < record.size(); ++index) {
        const char character = record[index];
        if (character == ':') {
            fields.push_back(field);
            field.clear();
            continue;
        }
        if (character != '\\') {
            field += character;
            continue;
        }
        if (++index >= record.size())
            return false;
        switch (record[index]) {
        case '\\': field += '\\'; break;
        case ':': field += ':'; break;
        case 'n': field += '\n'; break;
        case 'r': field += '\r'; break;
        default: return false;
        }
    }
    fields.push_back(field);
    return true;
}

template <std::size_t Size>
void copy_string(char (&destination)[Size], const std::string &source)
{
    std::snprintf(destination, Size, "%s", source.c_str());
}

bool exact_size(const std::list<std::string> &args, std::size_t size)
{
    return args.size() == size;
}

} // namespace

bool parse_api_request(const std::list<std::string> &args, ApiRequest &request)
{
    request = {};
    if (args.empty())
        return false;

    auto argument = args.begin();
    const std::string &command = *argument++;
    if (command == "Status") {
        request.command = ApiCommand::Status;
        return exact_size(args, 1);
    }
    if (command == "Scan") {
        request.command = ApiCommand::Scan;
        if (exact_size(args, 1))
            return true;
        return exact_size(args, 2) &&
            parse_integer(*argument, 0, CP0_WIFI_AP_MAX, request.scan_limit);
    }
    if (command == "Connect" || command == "ConnectHidden") {
        request.command = command == "Connect"
            ? ApiCommand::Connect : ApiCommand::ConnectHidden;
        if (args.size() != 2 && args.size() != 3)
            return false;
        request.ssid = *argument++;
        if (request.ssid.empty())
            return false;
        if (argument != args.end())
            request.password = *argument;
        return true;
    }
    if (command == "Disconnect") {
        request.command = ApiCommand::Disconnect;
        return exact_size(args, 1);
    }
    if (command == "ProfileForget" || command == "ProfileExists") {
        request.command = command == "ProfileForget" ? ApiCommand::ProfileForget : ApiCommand::ProfileExists;
        if (!exact_size(args, 2))
            return false;
        request.ssid = *argument;
        return !request.ssid.empty();
    }
    if (command == "ProfileDisconnectActive") {
        request.command = ApiCommand::ProfileDisconnectActive;
        return exact_size(args, 1);
    }
    if (command == "RadioEnabled") {
        request.command = ApiCommand::RadioEnabled;
        return exact_size(args, 1);
    }
    if (command == "RadioSetEnabled") {
        request.command = ApiCommand::RadioSetEnabled;
        if (!exact_size(args, 2))
            return false;
        const std::string &state = *argument;
        if (state == "on" || state == "1" || state == "true")
            request.radio_enabled = true;
        else if (state != "off" && state != "0" && state != "false")
            return false;
        return true;
    }
    return false;
}

const char *invalid_api_request_message()
{
    return "invalid wifi api request";
}

std::vector<ConnectionProfile> parse_connection_profiles(const std::string &payload)
{
    std::vector<ConnectionProfile> profiles;
    std::size_t start = 0;
    while (start < payload.size()) {
        const std::size_t end = payload.find('\n', start);
        std::string_view record(payload.data() + start,
            (end == std::string::npos ? payload.size() : end) - start);
        if (!record.empty() && record.back() == '\r') record.remove_suffix(1);
        start = end == std::string::npos ? payload.size() : end + 1;
        if (record.empty()) continue;

        const std::size_t first_separator = record.find(':');
        if (first_separator == std::string::npos) continue;
        const std::size_t second_separator = record.find(':', first_separator + 1);
        if (second_separator == std::string::npos) continue;

        ConnectionProfile profile;
        profile.uuid = std::string(record.substr(0, first_separator));
        profile.type = std::string(record.substr(
            first_separator + 1, second_separator - first_separator - 1));
        profile.name = std::string(record.substr(second_separator + 1));
        if (!profile.uuid.empty() && !profile.type.empty() && !profile.name.empty())
            profiles.push_back(std::move(profile));
    }
    return profiles;
}

std::string encode_status_payload(const cp0_wifi_status_t &status)
{
    return std::to_string(status.connected ? 1 : 0) + ':' + escape_field(status.ssid) + ':' +
        escape_field(status.ip) + ':' + std::to_string(status.signal) + ':' +
        std::to_string(status.ethernet ? 1 : 0);
}

bool decode_status_payload(const std::string &payload, cp0_wifi_status_t &status)
{
    std::vector<std::string> fields;
    int connected = 0;
    int signal = 0;
    int ethernet = 0;
    if (!split_record(payload, fields) || fields.size() != 5 ||
        !parse_integer(fields[0], 0, 1, connected) ||
        !parse_integer(fields[3], 0, 100, signal) ||
        !parse_integer(fields[4], 0, 1, ethernet))
        return false;

    cp0_wifi_status_t decoded{};
    decoded.connected = connected;
    copy_string(decoded.ssid, fields[1]);
    copy_string(decoded.ip, fields[2]);
    decoded.signal = signal;
    decoded.ethernet = ethernet;
    status = decoded;
    return true;
}

std::string encode_scan_payload(const cp0_wifi_ap_t *access_points, int count)
{
    if (!access_points || count <= 0)
        return {};
    std::string payload;
    for (int index = 0; index < count; ++index) {
        const auto &access_point = access_points[index];
        payload += escape_field(access_point.ssid) + ':' + std::to_string(access_point.signal) + ':' +
            escape_field(access_point.security) + ':' + std::to_string(access_point.in_use ? 1 : 0) + ':' +
            std::to_string(access_point.saved ? 1 : 0) + '\n';
    }
    return payload;
}

int decode_scan_payload(const std::string &payload, cp0_wifi_ap_t *access_points, int capacity)
{
    if (!access_points || capacity <= 0)
        return 0;
    int count = 0;
    std::size_t start = 0;
    while (start < payload.size() && count < capacity) {
        const std::size_t end = payload.find('\n', start);
        const std::string_view record(payload.data() + start,
            (end == std::string::npos ? payload.size() : end) - start);
        start = end == std::string::npos ? payload.size() : end + 1;
        if (record.empty())
            continue;

        std::vector<std::string> fields;
        int signal = 0;
        int in_use = 0;
        int saved = 0;
        if (!split_record(record, fields) || (fields.size() != 4 && fields.size() != 5) || fields[0].empty() ||
            !parse_integer(fields[1], 0, 100, signal) ||
            !parse_integer(fields[3], 0, 1, in_use) ||
            (fields.size() == 5 && !parse_integer(fields[4], 0, 1, saved)))
            continue;
        cp0_wifi_ap_t decoded{};
        copy_string(decoded.ssid, fields[0]);
        decoded.signal = signal;
        copy_string(decoded.security, fields[2]);
        decoded.in_use = in_use;
        decoded.saved = saved;
        access_points[count++] = decoded;
    }
    return count;
}

} // namespace cp0::network
