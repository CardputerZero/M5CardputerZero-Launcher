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

std::string update_job_label(UpdateAction action, int result_code,
                             const std::string &state)
{
    if (state == "running")
        return action == UpdateAction::CheckSystem
            ? "Refreshing package lists..."
            : "Checking launcher update...\nLauncher may restart";
    if (action == UpdateAction::CheckSystem)
        return result_code == 0 && state.rfind("succeeded:", 0) == 0
            ? "Package lists refreshed" : "System check failed";

    if (result_code == 0 && state.rfind("succeeded:", 0) == 0)
        return "Launcher updated";
    if (state.rfind("failed:", 0) == 0) {
        std::string stage = state.substr(7);
        const size_t detail = stage.find(':');
        if (detail != std::string::npos) stage.resize(detail);
        if (stage == "incompatible") return "No compatible update";
        if (stage == "version-not-newer") return "Launcher is up to date";
        if (stage == "download-package" || stage == "download-checksum")
            return "Update download failed";
        if (stage == "checksum" || stage == "checksum-manifest")
            return "Update verification failed";
        if (stage == "rollback-unavailable") return "Update blocked: no rollback";
    }
    return "Launcher update failed";
}

std::string launcher_state_label(const std::string &state)
{
    if (state.empty()) return {};
    if (state.rfind("succeeded:", 0) == 0) return "Launcher is up to date";
    if (state.rfind("failed:", 0) == 0)
        return update_job_label(UpdateAction::UpdateLauncher, -1, state);
    return "Updating: " + state;
}

bool extport_toggle_value(bool previous, bool desired, bool gpio_succeeded)
{
    return gpio_succeeded ? desired : previous;
}

} // namespace system_page
