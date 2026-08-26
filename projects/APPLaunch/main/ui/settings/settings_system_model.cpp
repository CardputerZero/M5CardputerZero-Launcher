#include "settings_system_model.hpp"

#include <cerrno>
#include <cstddef>
#include <sstream>
#include <vector>

namespace settings_system {
namespace {

constexpr const char *kUnavailable = "--";

std::string next_field(std::istringstream &lines)
{
    std::string value;
    if (!std::getline(lines, value)) return kUnavailable;
    if (!value.empty() && value.back() == '\r') value.pop_back();
    return value.empty() ? kUnavailable : value;
}

bool split_strict(const std::string &payload,
                  std::size_t expected_fields,
                  std::vector<std::string> &fields)
{
    fields.clear();
    if (!payload.empty() && payload.back() == '\n') return false;

    std::istringstream lines(payload);
    std::string field;
    while (std::getline(lines, field)) {
        if (!field.empty() && field.back() == '\r') field.pop_back();
        if (field.find('\r') != std::string::npos) return false;
        fields.push_back(std::move(field));
    }
    return fields.size() == expected_fields;
}

std::string normalized_field(const std::string &value)
{
    return value.empty() ? kUnavailable : value;
}

std::string update_failure_label(UpdateAction action,
                                 int result_code,
                                 const std::string &state)
{
    if (state == "cancelled" || state == "canceled") return "Update cancelled";
    if (state == "timeout" || state == "timed-out" || result_code == -ETIMEDOUT)
        return "Update timed out";
    return update_job_label(action, result_code, state);
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

bool parse_network_info_strict(const std::string &payload, NetworkInfo &result)
{
    std::vector<std::string> fields;
    if (!split_strict(payload, 3, fields)) return false;
    result = {normalized_field(fields[0]), normalized_field(fields[1]), normalized_field(fields[2])};
    return true;
}

bool parse_account_info_strict(const std::string &payload, AccountInfo &result)
{
    std::vector<std::string> fields;
    if (!split_strict(payload, 2, fields)) return false;
    result = {normalized_field(fields[0]), normalized_field(fields[1])};
    return true;
}

std::string version_label(const std::string &version)
{
    return "Version: " + (version.empty() ? std::string(kUnavailable) : version);
}

std::string build_label(const std::string &date,
                        const std::string &channel,
                        const std::string &commit)
{
    return "Build: " + (date.empty() ? std::string(kUnavailable) : date) + " " +
           (channel.empty() ? std::string("unknown") : channel) + " (" +
           (commit.empty() ? std::string("unknown") : commit) + ")";
}

const char *update_request(UpdateAction action)
{
    switch (action) {
    case UpdateAction::CheckSystem: return "AptUpdateStart";
    case UpdateAction::UpdateLauncher: return "UpdateLauncherStart";
    }
    return "";
}

const char *background_update_request(UpdateAction action)
{
    switch (action) {
    case UpdateAction::CheckSystem: return "AptUpdateBackground";
    case UpdateAction::UpdateLauncher: return "UpdateLauncherBackground";
    }
    return "";
}

std::string update_job_label(UpdateAction action,
                             int result_code,
                             const std::string &state)
{
    if (state == "running") {
        return action == UpdateAction::CheckSystem
            ? "Refreshing package lists..."
            : "Refreshing packages...\nChecking launcher update...\nLauncher may restart";
    }
    if (action == UpdateAction::UpdateLauncher) {
        if (state == "downloading") return "Downloading launcher update...";
        if (state == "repairing") return "Repairing package state...";
        if (state == "installing") return "Installing launcher update...";
        if (state == "restarting") return "Restarting Launcher...";
        if (state.rfind("recovering:", 0) == 0)
            return "Update failed; restoring previous version...";
    }
    if (state == "cancelled" || state == "canceled") return "Update cancelled";
    if (state == "timeout" || state == "timed-out" || result_code == -ETIMEDOUT)
        return "Update timed out";

    if (action == UpdateAction::CheckSystem)
        return result_code == 0 && state.rfind("succeeded:", 0) == 0
            ? "Package lists refreshed" : "System check failed";

    if (result_code == 0 && state.rfind("succeeded:", 0) == 0)
        return "Launcher updated";
    if (state.rfind("failed:", 0) == 0) {
        std::string stage = state.substr(7);
        const std::size_t detail = stage.find(':');
        if (detail != std::string::npos) stage.resize(detail);
        if (stage == "incompatible") return "No compatible update";
        if (stage == "version-not-newer") return "Launcher is up to date";
        if (stage == "download-package" || stage == "download-checksum")
            return "Update download failed";
        if (stage == "checksum" || stage == "checksum-manifest")
            return "Update verification failed";
        if (stage == "rollback-unavailable") return "Update blocked: no rollback";
        if (stage == "apt-update") return "System check failed";
    }
    return "Launcher update failed";
}

std::string launcher_state_label(const std::string &state)
{
    if (state.empty()) return {};
    if (state.rfind("succeeded:", 0) == 0) return "Launcher is up to date";
    if (state.rfind("failed:", 0) == 0)
        return update_job_label(UpdateAction::UpdateLauncher, -1, state);
    if (state == "running" || state == "downloading" || state == "repairing" ||
        state == "installing" || state == "restarting" ||
        state.rfind("recovering:", 0) == 0)
        return update_job_label(UpdateAction::UpdateLauncher, 0, state);
    if (state == "cancelled" || state == "canceled") return "Update cancelled";
    if (state == "timeout" || state == "timed-out") return "Update timed out";
    return "Updating: " + state;
}

std::string update_phase_label(UpdateAction action,
                               UpdatePhase phase,
                               int result_code,
                               const std::string &state)
{
    switch (phase) {
    case UpdatePhase::Idle: return "Ready to update";
    case UpdatePhase::Starting: return "Starting update...";
    case UpdatePhase::Running:
        return update_job_label(action, result_code, state.empty() ? "running" : state);
    case UpdatePhase::Succeeded: return update_job_label(action, result_code, state);
    case UpdatePhase::Failed: return update_failure_label(action, result_code, state);
    case UpdatePhase::TimedOut: return "Update timed out";
    case UpdatePhase::Cancelled: return "Update cancelled";
    }
    return "Update unavailable";
}

std::string backend_error_label(int result_code, const std::string &payload)
{
    if (!payload.empty()) return "Backend error: " + payload;
    if (result_code == -ETIMEDOUT) return "Backend request timed out";
    return "Backend unavailable (" + std::to_string(result_code) + ")";
}

std::string password_unsupported_label()
{
    return "Password changes are not supported";
}

} // namespace settings_system
