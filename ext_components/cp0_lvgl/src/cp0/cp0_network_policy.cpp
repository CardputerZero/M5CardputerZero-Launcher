#include "cp0_network_policy.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string_view>
#include <utility>

namespace cp0::network {
namespace {

void trim_carriage_return(std::string &line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}

void upsert_access_point(std::vector<AccessPoint> &access_points, AccessPoint candidate)
{
    auto existing = std::find_if(access_points.begin(), access_points.end(), [&](const AccessPoint &access_point) {
        return access_point.ssid == candidate.ssid;
    });
    if (existing == access_points.end()) {
        access_points.push_back(std::move(candidate));
        return;
    }

    const bool in_use = existing->in_use || candidate.in_use;
    if (candidate.signal > existing->signal)
        *existing = std::move(candidate);
    existing->in_use = in_use;
}

bool parse_signal(std::string_view text, int &signal)
{
    if (text.empty())
        return false;
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    // Wi-Fi RSSI is expressed in dBm. Keep accepting the historical 0..100
    // values so older backends remain readable while allowing real RSSI
    // values such as -42 dBm.
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed < -120 || parsed > 100)
        return false;
    signal = parsed;
    return true;
}

} // namespace

std::vector<std::string> split_terse_fields(const std::string &line)
{
    std::vector<std::string> fields;
    std::string current;
    for (size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (character == '\\' && index + 1 < line.size()) {
            current.push_back(line[++index]);
        } else if (character == ':') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    fields.push_back(current);
    return fields;
}

std::vector<AccessPoint> parse_scan_output(const std::string &output)
{
    std::vector<AccessPoint> access_points;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        trim_carriage_return(line);
        if (line.empty())
            continue;

        const auto fields = split_terse_fields(line);
        if (fields.size() != 4 || fields[0].empty())
            continue;

        AccessPoint access_point;
        access_point.ssid = fields[0];
        if (!parse_signal(fields[1], access_point.signal))
            continue;
        access_point.security = fields[2];
        access_point.in_use = fields[3].find('*') != std::string::npos;
        upsert_access_point(access_points, std::move(access_point));
    }
    return access_points;
}

Status parse_device_status(const std::string &output)
{
    Status status;
    bool wifi_found = false;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        trim_carriage_return(line);
        const auto fields = split_terse_fields(line);
        if (fields.size() != 4)
            continue;

        const bool connected = fields[2].rfind("connected", 0) == 0;
        const bool has_connection = !fields[3].empty() && fields[3] != "--";
        if (fields[1] == "ethernet" && connected) {
            status.ethernet = true;
            continue;
        }
        if (fields[1] != "wifi" || wifi_found)
            continue;

        wifi_found = true;
        if (!connected && !has_connection)
            continue;
        status.connected = true;
        status.wifi_interface = fields[0];
        if (has_connection) {
            status.ssid = fields[3];
            const std::string prefix = "netplan-" + fields[0] + "-";
            if (status.ssid.rfind(prefix, 0) == 0)
                status.ssid.erase(0, prefix.size());
        }
    }
    return status;
}

void apply_active_wifi(const std::string &output, Status &status)
{
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        trim_carriage_return(line);
        const auto fields = split_terse_fields(line);
        if (fields.size() != 3 || fields[0] != "*")
            continue;
        if (!parse_signal(fields[1], status.signal))
            continue;
        if (status.ssid.empty() && fields.size() >= 3)
            status.ssid = fields[2];
        return;
    }
}

std::string parse_ipv4_address(const std::string &output)
{
    const size_t address_start = output.find("inet ");
    if (address_start == std::string::npos)
        return {};

    const size_t value_start = address_start + 5;
    size_t value_end = output.find('/', value_start);
    if (value_end == std::string::npos)
        value_end = output.find_first_of(" \t\r\n", value_start);
    return output.substr(value_start, value_end - value_start);
}

bool parse_radio_enabled(const std::string &output)
{
    const size_t start = output.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return false;
    const size_t end = output.find_last_not_of(" \t\r\n");
    return output.substr(start, end - start + 1) == "enabled";
}

std::string first_active_connection(const std::string &output)
{
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        trim_carriage_return(line);
        const auto fields = split_terse_fields(line);
        if (!fields.empty() && !fields[0].empty() && fields[0] != "lo")
            return fields[0];
    }
    return {};
}

} // namespace cp0::network
