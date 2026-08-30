#pragma once

#include <string>
#include <utility>

namespace settings_system {

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

struct UpdateStatusInfo
{
    enum class Progress : int {
        Unknown = -1,
    };

    bool terminal = false;
    bool availability_known = false;
    bool available = false;
    std::string version;
    std::string commit;
    int progress = static_cast<int>(Progress::Unknown);
    std::string stage;
};

enum class UpdateAction
{
    CheckSystem,
    UpdateLauncher,
};

enum class UpdatePhase
{
    Idle,
    Starting,
    Running,
    Succeeded,
    Failed,
    TimedOut,
    Cancelled,
};

NetworkInfo parse_network_info(const std::string &payload);
AccountInfo parse_account_info(const std::string &payload);
bool parse_network_info_strict(const std::string &payload, NetworkInfo &result);
bool parse_account_info_strict(const std::string &payload, AccountInfo &result);
UpdateStatusInfo parse_update_status(const std::string &state,
                                     const std::string &payload = {});

std::string version_label(const std::string &version);
std::string build_label(const std::string &date,
                        const std::string &channel,
                        const std::string &commit);
const char *update_request(UpdateAction action);
const char *background_update_request(UpdateAction action);
std::string update_job_label(UpdateAction action,
                             int result_code,
                             const std::string &state);
std::string launcher_state_label(const std::string &state);
std::string update_phase_label(UpdateAction action,
                               UpdatePhase phase,
                               int result_code = 0,
                               const std::string &state = {});
std::string backend_error_label(int result_code, const std::string &payload = {});
std::string password_unsupported_label();

} // namespace settings_system

namespace system_page {

using NetworkInfo = settings_system::NetworkInfo;
using AccountInfo = settings_system::AccountInfo;
using UpdateStatusInfo = settings_system::UpdateStatusInfo;
using UpdateAction = settings_system::UpdateAction;
using UpdatePhase = settings_system::UpdatePhase;

template <typename Value>
bool commit_if_success(bool success, Value candidate, Value &current)
{
    return settings_system::commit_if_success(success, std::move(candidate), current);
}

inline NetworkInfo parse_network_info(const std::string &payload)
{
    return settings_system::parse_network_info(payload);
}

inline AccountInfo parse_account_info(const std::string &payload)
{
    return settings_system::parse_account_info(payload);
}

inline bool parse_network_info_strict(const std::string &payload, NetworkInfo &result)
{
    return settings_system::parse_network_info_strict(payload, result);
}

inline bool parse_account_info_strict(const std::string &payload, AccountInfo &result)
{
    return settings_system::parse_account_info_strict(payload, result);
}

inline UpdateStatusInfo parse_update_status(const std::string &state,
                                            const std::string &payload = {})
{
    return settings_system::parse_update_status(state, payload);
}

inline std::string version_label(const std::string &version)
{
    return settings_system::version_label(version);
}

inline std::string build_label(const std::string &date,
                               const std::string &channel,
                               const std::string &commit)
{
    return settings_system::build_label(date, channel, commit);
}

inline const char *update_request(UpdateAction action)
{
    return settings_system::update_request(action);
}

inline const char *background_update_request(UpdateAction action)
{
    return settings_system::background_update_request(action);
}

inline std::string update_job_label(UpdateAction action,
                                    int result_code,
                                    const std::string &state)
{
    return settings_system::update_job_label(action, result_code, state);
}

inline std::string launcher_state_label(const std::string &state)
{
    return settings_system::launcher_state_label(state);
}

inline std::string update_phase_label(UpdateAction action,
                                      UpdatePhase phase,
                                      int result_code = 0,
                                      const std::string &state = {})
{
    return settings_system::update_phase_label(action, phase, result_code, state);
}

inline std::string backend_error_label(int result_code, const std::string &payload = {})
{
    return settings_system::backend_error_label(result_code, payload);
}

inline std::string password_unsupported_label()
{
    return settings_system::password_unsupported_label();
}

inline bool extport_toggle_value(bool previous, bool desired, bool gpio_succeeded)
{
    return gpio_succeeded ? desired : previous;
}

} // namespace system_page
