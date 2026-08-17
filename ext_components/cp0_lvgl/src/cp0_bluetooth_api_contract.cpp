#include "cp0_bluetooth_api_contract.hpp"

#include <charconv>
#include <cctype>
#include <iterator>
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

bool parse_session_id(std::string_view text, uint64_t &value)
{
    if (text.empty()) return false;
    uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
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

bool parse_request(const std::list<std::string> &arguments, Request &request)
{
    request = {};
    if (arguments.empty()) return false;
    const std::string &command = arguments.front();
    auto argument = std::next(arguments.begin());

    if (command == "BtSessionInit") {
        request.command = Command::SessionInit;
        return arguments.size() == 1;
    }
    if (command == "BtSessionDeinit" || command == "BtStatusGet" ||
        command == "BtConnectedListInit" || command == "BtConnectedListGet" ||
        command == "BtConnectedListDeinit" || command == "BtScanOn" ||
        command == "BtScanOff") {
        if (arguments.size() != 2 || !parse_session_id(*argument, request.session_id))
            return false;
        request.command = command == "BtSessionDeinit" ? Command::SessionDeinit :
            (command == "BtStatusGet" ? Command::StatusGet :
             (command == "BtConnectedListInit" ? Command::ConnectedListInit :
              (command == "BtConnectedListGet" ? Command::ConnectedListGet :
               (command == "BtConnectedListDeinit" ? Command::ConnectedListDeinit :
                (command == "BtScanOn" ? Command::ScanOn : Command::ScanOff)))));
        return true;
    }

    if (command == "BtStatus" || command == "BtDiscoveryStart" ||
        command == "BtDiscoveryStop") {
        if (arguments.size() != 1) return false;
        request.command = command == "BtStatus" ? Command::Status :
            (command == "BtDiscoveryStart" ? Command::DiscoveryStart : Command::DiscoveryStop);
        return true;
    }
    if (command == "BtPower" || command == "BtDiscoverable") {
        request.command = command == "BtPower" ? Command::Power : Command::Discoverable;
        return arguments.size() == 2 && parse_integer(*argument, 0, 1, request.value);
    }
    if (command == "BtAlias") {
        request.command = Command::Alias;
        if (arguments.size() != 2 || argument->empty() || argument->size() >= 64 ||
            has_wire_control(*argument))
            return false;
        request.text = *argument;
        return true;
    }
    if (command == "BtScan" || command == "BtList" || command == "BtConnectedList") {
        request.command = command == "BtScan" ? Command::Scan :
            (command == "BtList" ? Command::List : Command::ConnectedList);
        if (arguments.size() == 1) return true;
        return arguments.size() == 2 && parse_integer(*argument, 1, 16, request.max_count);
    }
    if (command == "BtPair" || command == "BtConnect" || command == "BtDisconnect" ||
        command == "BtRemove") {
        request.command = command == "BtPair" ? Command::Pair :
            (command == "BtConnect" ? Command::Connect :
             (command == "BtDisconnect" ? Command::Disconnect : Command::Remove));
        if (arguments.size() != 2 || !valid_address(*argument)) return false;
        request.text = *argument;
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
