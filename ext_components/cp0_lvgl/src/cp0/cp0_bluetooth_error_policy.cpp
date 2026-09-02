#include "cp0_bluetooth_error_policy.hpp"

#include <algorithm>
#include <cctype>

namespace cp0::bluetooth::policy {
namespace {

bool equals_ignore_case(const std::string &left, const std::string &right)
{
    if (left.size() != right.size())
        return false;
    return std::equal(left.begin(), left.end(), right.begin(),
                      [](unsigned char lhs, unsigned char rhs) {
                          return std::toupper(lhs) == std::toupper(rhs);
                      });
}

bool is_printable_agent_text(const std::string &text)
{
    return !text.empty() && text.size() <= 16 &&
           std::all_of(text.begin(), text.end(), [](unsigned char character) {
               return character >= 0x20 && character != 0x7f;
           });
}

} // namespace

bool is_pair_already_exists(const std::string &error_name)
{
    return error_name == "org.bluez.Error.AlreadyExists" ||
           error_name == "org.bluez.Error.AlreadyPaired";
}

bool is_idempotent_success(const std::string &command,
                           const std::string &error_name)
{
    if (is_pair_already_exists(error_name))
        return command == "pair";
    if (command == "connect")
        return error_name == "org.bluez.Error.AlreadyConnected" ||
               error_name == "org.bluez.Error.InProgress";
    if (command == "disconnect")
        return error_name == "org.bluez.Error.NotConnected";
    if (command == "start")
        return error_name == "org.bluez.Error.InProgress";
    if (command == "stop")
        return error_name == "org.bluez.Error.NotReady" ||
               error_name == "org.bluez.Error.NotAuthorized";
    return false;
}

bool pair_requires_force_cleanup(const std::string &error_name)
{
    // InProgress is a live transaction, not a stale bond. Cancel it without
    // force-removing the Device1 object; a later completion decides whether
    // recovery is needed.
    return !is_pair_already_exists(error_name) &&
           error_name != "org.bluez.Error.InProgress";
}

bool connected_snapshot_matches(const std::string &requested_address,
                                const std::string &snapshot_address,
                                bool connected)
{
    return connected && equals_ignore_case(requested_address, snapshot_address);
}

bool is_agent_request_method(const std::string &method)
{
    return method == "RequestPinCode" || method == "RequestPasskey" ||
           method == "RequestConfirmation" || method == "RequestAuthorization" ||
           method == "AuthorizeService";
}

bool is_agent_display_method(const std::string &method)
{
    return method == "DisplayPinCode" || method == "DisplayPasskey";
}

bool agent_reply_valid(const std::string &method, bool accepted,
                       const std::string &text)
{
    if (!accepted)
        return true;
    if (method == "RequestPinCode")
        return is_printable_agent_text(text);
    if (method == "RequestPasskey")
        return text.size() == 6 &&
               std::all_of(text.begin(), text.end(), [](unsigned char character) {
                   return character >= '0' && character <= '9';
               });
    return is_agent_request_method(method);
}

bool agent_reply_requires_cleanup(const std::string &method, bool accepted,
                                  const std::string &text)
{
    return !accepted || !agent_reply_valid(method, accepted, text);
}

} // namespace cp0::bluetooth::policy
