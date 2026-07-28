#pragma once

#include <string>
#include <utility>

namespace system_page {

template <typename Value>
bool commit_if_success(bool success, Value candidate, Value &current)
{
    if (!success) return false;
    current = std::move(candidate);
    return true;
}

struct NetworkInfo
{
    std::string ip;
    std::string gateway;
    std::string mac;
};

struct AccountInfo
{
    std::string username;
    std::string hostname;
};

enum class UpdateAction
{
    CheckSystem,
    UpdateLauncher,
};

NetworkInfo parse_network_info(const std::string &payload);
AccountInfo parse_account_info(const std::string &payload);
std::string version_label(const std::string &version);
std::string build_label(const std::string &date, const std::string &channel,
                        const std::string &commit);
const char *update_request(UpdateAction action);
bool extport_toggle_value(bool previous, bool desired, bool gpio_succeeded);

} // namespace system_page
