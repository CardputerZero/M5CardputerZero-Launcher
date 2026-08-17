#include "cp0_bluetooth_api_contract.hpp"

#include <charconv>
#include <cctype>
#include <iterator>
#include <sstream>
#include <string_view>

namespace cp0::bluetooth {
namespace {

bool parse_integer(std::string_view text, int minimum, int maximum, int &value)
{
    if (text.empty()) return false;
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed < minimum || parsed > maximum)
        return false;
    value = parsed;
    return true;
}

bool valid_address(const std::string &address)
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

bool has_wire_control(const std::string &value)
{
    for (unsigned char character : value)
        if (character < 0x20 || character == 0x7f) return true;
    return false;
}

} // namespace

bool valid_session_id(const std::string &session_id)
{
    if (session_id.empty() || session_id.size() > 20)
        return false;
    for (unsigned char character : session_id)
        if (!std::isdigit(character))
            return false;
    return true;
}

// Returns true and advances `argument`/sets session info when a session-scoped
// form `<command> <session_id> [payload]` is recognized. Keeps legacy forms
// (no session id) intact by checking the argument count.
static bool parse_session_scoped_command(const std::list<std::string> &arguments,
                                         Command legacy,
                                         Command session, Request &request)
{
    auto argument = std::next(arguments.begin());

    // Legacy: <command> <payload> (2 arguments total).
    if (arguments.size() == 2) {
        request.command = legacy;
        if (legacy == Command::Power || legacy == Command::Discoverable)
            return parse_integer(*argument, 0, 1, request.value);
        if (legacy == Command::Alias) {
            if (argument->empty() || argument->size() >= 64 || has_wire_control(*argument))
                return false;
            request.text = *argument;
            return true;
        }
        // Pair / Connect / Disconnect / Remove.
        if (!valid_address(*argument)) return false;
        request.text = *argument;
        return true;
    }

    // Session: <command> <session_id> <payload> (3 arguments total).
    if (arguments.size() != 3 || !valid_session_id(*argument)) return false;
    request.command = session;
    request.has_session = true;
    request.session_id = *argument;
    const std::string &payload = *std::next(argument);
    if (session == Command::Power || session == Command::Discoverable)
        return parse_integer(payload, 0, 1, request.value);
    if (session == Command::Alias) {
        if (payload.empty() || payload.size() >= 64 || has_wire_control(payload))
            return false;
        request.text = payload;
        return true;
    }
    if (!valid_address(payload)) return false;
    request.text = payload;
    return true;
}

bool parse_request(const std::list<std::string> &arguments, Request &request)
{
    request = {};
    if (arguments.empty()) return false;
    const std::string &command = arguments.front();
    auto argument = std::next(arguments.begin());

    // Legacy status / discovery (no payload, no session).
    if (command == "BtStatus" || command == "BtDiscoveryStart" ||
        command == "BtDiscoveryStop") {
        if (arguments.size() != 1) return false;
        request.command = command == "BtStatus" ? Command::Status :
            (command == "BtDiscoveryStart" ? Command::DiscoveryStart : Command::DiscoveryStop);
        return true;
    }
    if (command == "BtScan" || command == "BtList" || command == "BtConnectedList") {
        request.command = command == "BtScan" ? Command::Scan :
            (command == "BtList" ? Command::List : Command::ConnectedList);
        if (arguments.size() == 1) return true;
        return arguments.size() == 2 && parse_integer(*argument, 1, 16, request.max_count);
    }

    if (command == "BtPower")
        return parse_session_scoped_command(arguments, Command::Power, Command::Power, request);
    if (command == "BtDiscoverable")
        return parse_session_scoped_command(arguments, Command::Discoverable,
                                            Command::Discoverable, request);
    if (command == "BtAlias")
        return parse_session_scoped_command(arguments, Command::Alias, Command::Alias, request);
    if (command == "BtPair")
        return parse_session_scoped_command(arguments, Command::Pair, Command::Pair, request);
    if (command == "BtConnect")
        return parse_session_scoped_command(arguments, Command::Connect, Command::Connect, request);
    if (command == "BtDisconnect")
        return parse_session_scoped_command(arguments, Command::Disconnect,
                                            Command::Disconnect, request);
    if (command == "BtRemove")
        return parse_session_scoped_command(arguments, Command::Remove, Command::Remove, request);

    // Session lifecycle commands.
    if (command == "BtSessionInit") {
        if (arguments.size() != 1) return false;
        request.command = Command::SessionInit;
        return true;
    }
    if (command == "BtSessionDeinit") {
        if (arguments.size() != 2 || !valid_session_id(*argument)) return false;
        request.command = Command::SessionDeinit;
        request.has_session = true;
        request.session_id = *argument;
        return true;
    }
    if (command == "BtStatusGet") {
        if (arguments.size() != 2 || !valid_session_id(*argument)) return false;
        request.command = Command::StatusGet;
        request.has_session = true;
        request.session_id = *argument;
        return true;
    }
    if (command == "BtConnectedListInit" || command == "BtConnectedListGet" ||
        command == "BtConnectedListDeinit" || command == "BtScanOn" ||
        command == "BtScanOff") {
        if (arguments.size() != 2 || !valid_session_id(*argument)) return false;
        request.command = command == "BtConnectedListInit" ? Command::ConnectedListInit :
            (command == "BtConnectedListGet" ? Command::ConnectedListGet :
             (command == "BtConnectedListDeinit" ? Command::ConnectedListDeinit :
              (command == "BtScanOn" ? Command::ScanOn : Command::ScanOff)));
        request.has_session = true;
        request.session_id = *argument;
        return true;
    }
    return false;
}

std::string sanitize_wire_field(std::string value)
{
    for (char &character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7f)
            character = ' ';
    }
    return value;
}

std::string encode_status(const cp0_bt_status_t &status)
{
    std::ostringstream output;
    output << status.powered << '\t'
           << sanitize_wire_field(status.address) << '\t'
           << status.discoverable << '\t'
           << sanitize_wire_field(status.alias);
    return output.str();
}

std::string encode_devices(const cp0_bt_device_t *devices, int count)
{
    std::ostringstream output;
    for (int index = 0; devices && index < count; ++index) {
        output << sanitize_wire_field(devices[index].address) << '\t'
               << devices[index].rssi << '\t'
               << devices[index].connected << '\t' << devices[index].paired << '\t'
               << devices[index].trusted << '\t'
               << sanitize_wire_field(devices[index].name) << '\n';
    }
    return output.str();
}

void invoke_callback(const std::function<void(int, std::string)> &callback,
                     int code, const std::string &data) noexcept
{
    if (!callback) return;
    try {
        callback(code, data);
    } catch (...) {
    }
}

} // namespace cp0::bluetooth
