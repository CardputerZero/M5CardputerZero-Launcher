#include "system_page_model.hpp"

#include <sstream>

namespace system_page {
namespace {

std::string next_field(std::istringstream &lines)
{
    std::string value;
    if (!std::getline(lines, value))
        return "--";
    if (!value.empty() && value.back() == '\r')
        value.pop_back();
    return value.empty() ? "--" : value;
}

} // namespace

NetworkInfo parse_network_info(const std::string &payload)
{
    std::istringstream lines(payload);
    return {next_field(lines), next_field(lines), next_field(lines)};
}

AccountInfo parse_account_info(const std::string &payload)
{
    std::istringstream lines(payload);
    return {next_field(lines), next_field(lines)};
}

std::string version_label(const std::string &version)
{
    return "Version: " + (version.empty() ? std::string("--") : version);
}

std::string build_label(const std::string &date, const std::string &channel,
                        const std::string &commit)
{
    return "Build: " + (date.empty() ? std::string("--") : date) + " " +
           (channel.empty() ? std::string("unknown") : channel) + " (" +
           (commit.empty() ? std::string("unknown") : commit) + ")";
}

const char *update_request(UpdateAction action)
{
    switch (action) {
    case UpdateAction::CheckSystem:
        return "AptUpdateStart";
    case UpdateAction::UpdateLauncher:
        return "UpdateLauncherStart";
    }
    return "";
}

bool extport_toggle_value(bool previous, bool desired, bool gpio_succeeded)
{
    return gpio_succeeded ? desired : previous;
}

} // namespace system_page
