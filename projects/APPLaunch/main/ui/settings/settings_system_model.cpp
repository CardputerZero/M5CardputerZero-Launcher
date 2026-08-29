#include "settings_system_model.hpp"

#include <cerrno>
#include <cctype>
#include <cstddef>
#include <cstdlib>
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

std::string trim_copy(const std::string &value)
{
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;
    return value.substr(first, last - first);
}

std::string lowercase_ascii(std::string value)
{
    for (char &character : value)
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    return value;
}

bool parse_progress(const std::string &text, int &progress)
{
    std::string value = trim_copy(text);
    if (!value.empty() && value.back() == '%') {
        value.pop_back();
        value = trim_copy(value);
    }
    if (value.empty()) return false;

    errno = 0;
    char *end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str() || *end != '\0') return false;
    if (parsed < 0) progress = 0;
    else if (parsed > 100) progress = 100;
    else progress = static_cast<int>(parsed);
    return true;
}

bool parse_boolean(const std::string &text, bool &value)
{
    const std::string normalized = lowercase_ascii(trim_copy(text));
    if (normalized == "1" || normalized == "true" || normalized == "yes") {
        value = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no") {
        value = false;
        return true;
    }
    return false;
}

bool is_progress_stage(const std::string &stage)
{
    return stage == "checking" || stage == "downloading" ||
           stage == "verifying" || stage == "repairing" ||
           stage == "installing" || stage == "restarting" ||
           stage == "recovering";
}

void apply_update_token(const std::string &raw_token, UpdateStatusInfo &result)
{
    const std::string token = trim_copy(raw_token);
    if (token.empty()) return;

    const std::size_t separator = token.find('=');
    if (separator == std::string::npos) {
        const std::string marker = lowercase_ascii(token);
        if (marker == "update-available") {
            result.availability_known = true;
            result.available = true;
        } else if (marker == "up-to-date" || marker == "no-update") {
            result.availability_known = true;
            result.available = false;
        }
        return;
    }

    const std::string key = lowercase_ascii(trim_copy(token.substr(0, separator)));
    const std::string value = trim_copy(token.substr(separator + 1));
    if (key == "version") result.version = value;
    else if (key == "commit") result.commit = value;
    else if (key == "stage" && !value.empty()) result.stage = lowercase_ascii(value);
    else if (key == "progress") {
        int progress = -1;
        result.progress = parse_progress(value, progress) ? progress : -1;
    } else if (key == "available") {
        bool available = false;
        if (parse_boolean(value, available)) {
            result.availability_known = true;
            result.available = available;
        }
    }
}

void apply_update_record(const std::string &raw_record, UpdateStatusInfo &result)
{
    std::string record = trim_copy(raw_record);
    if (record.empty()) return;
    for (char &character : record)
        if (character == '\n' || character == '\r') character = '|';

    const std::size_t metadata = record.find('|');
    const std::string head = trim_copy(record.substr(0, metadata));
    const std::string normalized_head = lowercase_ascii(head);
    bool head_consumed = false;

    if (normalized_head.rfind("succeeded:", 0) == 0 || normalized_head == "succeeded") {
        result.terminal = true;
        result.stage = "succeeded";
        head_consumed = true;
        const std::size_t separator = head.find(':');
        const std::string detail = separator == std::string::npos
            ? std::string() : trim_copy(head.substr(separator + 1));
        const std::string normalized_detail = lowercase_ascii(detail);
        if (normalized_detail == "update-available") {
            result.availability_known = true;
            result.available = true;
        } else if (normalized_detail == "up-to-date" || normalized_detail == "no-update") {
            result.availability_known = true;
            result.available = false;
        } else if (!detail.empty()) {
            // Legacy `succeeded:<version>` means the installed launcher is current.
            result.availability_known = true;
            result.available = false;
            result.version = detail;
        }
    } else if (normalized_head.rfind("failed:", 0) == 0 || normalized_head == "failed") {
        result.terminal = true;
        result.stage = "failed";
        head_consumed = true;
    } else if (normalized_head == "cancelled" || normalized_head == "canceled" ||
               normalized_head == "timeout" || normalized_head == "timed-out") {
        result.terminal = true;
        result.stage = normalized_head;
        head_consumed = true;
    } else {
        const std::size_t separator = normalized_head.find(':');
        const std::string stage = normalized_head.substr(0, separator);
        if (is_progress_stage(stage)) {
            result.stage = stage;
            head_consumed = true;
            if (separator != std::string::npos) {
                int progress = -1;
                result.progress = parse_progress(head.substr(separator + 1), progress)
                    ? progress : -1;
            }
        } else if (normalized_head == "running") {
            result.stage = "running";
            head_consumed = true;
        }
    }

    if (!head_consumed) apply_update_token(head, result);
    std::size_t start = metadata == std::string::npos ? record.size() : metadata + 1;
    while (start < record.size()) {
        const std::size_t end = record.find('|', start);
        apply_update_token(record.substr(start, end - start), result);
        if (end == std::string::npos) break;
        start = end + 1;
    }
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

UpdateStatusInfo parse_update_status(const std::string &state,
                                     const std::string &payload)
{
    UpdateStatusInfo result;
    apply_update_record(state, result);
    apply_update_record(payload, result);
    return result;
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
