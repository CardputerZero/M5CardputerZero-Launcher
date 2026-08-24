#pragma once

#include "cp0_lvgl_app.h"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace settings_bluetooth {

struct StatusRecord {
    bool powered = false;
    std::string address;
    bool discoverable = false;
    std::string alias;
};

struct DeviceRecord {
    std::string address;
    int rssi = 0;
    bool connected = false;
    bool paired = false;
    bool trusted = false;
    std::string name;
};

constexpr std::size_t kMaxAliasBytes = CP0_BT_NAME_MAX - 1;
constexpr std::size_t kMaxAddressBytes =
    sizeof(((cp0_bt_status_t *)nullptr)->address) - 1;
constexpr std::size_t kMaxDeviceNameBytes = CP0_BT_NAME_MAX - 1;

inline bool parse_boolean(std::string_view value, bool &output)
{
    if (value != "0" && value != "1") return false;
    output = value == "1";
    return true;
}

inline bool parse_integer(std::string_view value, int &output)
{
    if (value.empty()) return false;
    int parsed = 0;
    const char *begin = value.data();
    const char *end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end) return false;
    output = parsed;
    return true;
}

inline bool has_wire_control(std::string_view value)
{
    for (const unsigned char byte : value) {
        if (byte < 0x20 || byte == 0x7f) return true;
    }
    return false;
}

inline bool valid_utf8(std::string_view value)
{
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }

        std::size_t width = 0;
        unsigned char second_min = 0x80;
        unsigned char second_max = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
        } else if (first == 0xe0) {
            width = 3;
            second_min = 0xa0;
        } else if (first >= 0xe1 && first <= 0xec) {
            width = 3;
        } else if (first == 0xed) {
            width = 3;
            second_max = 0x9f;
        } else if (first >= 0xee && first <= 0xef) {
            width = 3;
        } else if (first == 0xf0) {
            width = 4;
            second_min = 0x90;
        } else if (first >= 0xf1 && first <= 0xf3) {
            width = 4;
        } else if (first == 0xf4) {
            width = 4;
            second_max = 0x8f;
        } else {
            return false;
        }

        if (index + width > value.size()) return false;
        const unsigned char second = static_cast<unsigned char>(value[index + 1]);
        if (second < second_min || second > second_max) return false;
        for (std::size_t continuation = 2; continuation < width; ++continuation) {
            const unsigned char byte =
                static_cast<unsigned char>(value[index + continuation]);
            if (byte < 0x80 || byte > 0xbf) return false;
        }
        index += width;
    }
    return true;
}

inline bool valid_text_field(std::string_view value,
                             std::size_t maximum_bytes,
                             bool allow_empty = true)
{
    if ((!allow_empty && value.empty()) || value.size() > maximum_bytes) return false;
    return !has_wire_control(value) && valid_utf8(value);
}

inline bool valid_device_address(std::string_view address)
{
    if (address.size() != 17) return false;
    for (std::size_t index = 0; index < address.size(); ++index) {
        if ((index + 1) % 3 == 0) {
            if (address[index] != ':') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(address[index]))) {
            return false;
        }
    }
    return true;
}

inline bool split_fields(std::string_view record,
                         std::size_t expected_count,
                         std::vector<std::string_view> &fields)
{
    fields.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t separator = record.find('\t', start);
        fields.emplace_back(record.substr(
            start,
            separator == std::string_view::npos ? std::string_view::npos : separator - start));
        if (separator == std::string_view::npos) break;
        start = separator + 1;
    }
    return fields.size() == expected_count;
}

inline bool decode_status(std::string_view payload, StatusRecord &output)
{
    std::vector<std::string_view> fields;
    if (!split_fields(payload, 4, fields)) return false;

    StatusRecord decoded;
    if (!parse_boolean(fields[0], decoded.powered) ||
        !parse_boolean(fields[2], decoded.discoverable))
        return false;

    if (fields[1].size() > kMaxAddressBytes || has_wire_control(fields[1])) return false;
    if (decoded.powered && fields[1].empty()) return false;
    if (!fields[1].empty() && !valid_device_address(fields[1])) return false;
    if (!valid_text_field(fields[3], kMaxAliasBytes)) return false;

    decoded.address.assign(fields[1]);
    decoded.alias.assign(fields[3]);
    output = std::move(decoded);
    return true;
}

inline bool decode_device(std::string_view payload, DeviceRecord &output)
{
    std::vector<std::string_view> fields;
    if (!split_fields(payload, 6, fields)) return false;

    DeviceRecord decoded;
    if (!valid_device_address(fields[0]) || !parse_integer(fields[1], decoded.rssi) ||
        !parse_boolean(fields[2], decoded.connected) ||
        !parse_boolean(fields[3], decoded.paired) ||
        !parse_boolean(fields[4], decoded.trusted) ||
        !valid_text_field(fields[5], kMaxDeviceNameBytes))
        return false;

    decoded.address.assign(fields[0]);
    decoded.name.assign(fields[5]);
    output = std::move(decoded);
    return true;
}

inline bool decode_devices(std::string_view payload,
                           std::vector<DeviceRecord> &output,
                           int maximum_count = CP0_BT_DEVICE_MAX)
{
    output.clear();
    if (maximum_count < 0 || maximum_count > CP0_BT_DEVICE_MAX) return false;
    if (payload.empty()) return true;

    if (payload.back() == '\n') {
        payload.remove_suffix(1);
        if (payload.empty() || payload.back() == '\n') return false;
    }

    std::size_t start = 0;
    while (start < payload.size()) {
        const std::size_t separator = payload.find('\n', start);
        const std::size_t end = separator == std::string_view::npos
            ? payload.size()
            : separator;
        const std::string_view line = payload.substr(start, end - start);
        if (line.empty() || static_cast<int>(output.size()) >= maximum_count) {
            return false;
        }

        DeviceRecord device;
        if (!decode_device(line, device)) return false;
        output.push_back(std::move(device));

        if (separator == std::string_view::npos) break;
        start = separator + 1;
    }
    return true;
}

inline bool decode_device_list_reply(int code,
                                     std::string_view payload,
                                     std::vector<DeviceRecord> &output,
                                     int maximum_count = CP0_BT_DEVICE_MAX)
{
    output.clear();
    if (code < 0 || code > maximum_count) return false;
    if (!decode_devices(payload, output, maximum_count)) return false;
    return static_cast<int>(output.size()) == code;
}

inline bool valid_alias(std::string_view alias)
{
    return valid_text_field(alias, kMaxAliasBytes, false);
}

inline bool success_without_payload(int code, std::string_view payload)
{
    return code == 0 && (payload.empty() || payload == "ok");
}

inline std::list<std::string> status_request()
{
    return {"BtStatus"};
}

inline std::list<std::string> power_request(bool enabled)
{
    return {"BtPower", enabled ? "1" : "0"};
}

inline std::list<std::string> discoverable_request(bool enabled)
{
    return {"BtDiscoverable", enabled ? "1" : "0"};
}

inline bool alias_request(std::string_view alias, std::list<std::string> &request)
{
    if (!valid_alias(alias)) return false;
    request = {"BtAlias", std::string(alias)};
    return true;
}

inline bool list_request(bool connected_only, int maximum_count, std::list<std::string> &request)
{
    if (maximum_count < 1 || maximum_count > CP0_BT_DEVICE_MAX) return false;
    request = {connected_only ? "BtConnectedList" : "BtList",
               std::to_string(maximum_count)};
    return true;
}

inline bool scan_request(int maximum_count, std::list<std::string> &request)
{
    if (maximum_count < 1 || maximum_count > CP0_BT_DEVICE_MAX) return false;
    request = {"BtScan", std::to_string(maximum_count)};
    return true;
}

inline bool discovery_start_request(std::list<std::string> &request)
{
    request = {"BtDiscoveryStart"};
    return true;
}

inline std::list<std::string> discovery_start_request()
{
    return {"BtDiscoveryStart"};
}

inline bool discovery_stop_request(std::list<std::string> &request)
{
    request = {"BtDiscoveryStop"};
    return true;
}

inline std::list<std::string> discovery_stop_request()
{
    return {"BtDiscoveryStop"};
}

inline bool device_request(const char *command,
                           std::string_view address,
                           std::list<std::string> &request)
{
    if (!command || !valid_device_address(address)) return false;
    const std::string command_text(command);
    if (command_text != "BtPair" && command_text != "BtConnect" &&
        command_text != "BtDisconnect" && command_text != "BtRemove")
        return false;
    request = {command_text, std::string(address)};
    return true;
}

} // namespace settings_bluetooth
