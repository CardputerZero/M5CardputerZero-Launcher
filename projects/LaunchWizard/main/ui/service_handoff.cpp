#include "service_handoff.h"

namespace launch_wizard {
namespace {

std::string command_error(const char *step, const HandoffCommandResult &result)
{
    std::string error = step;
    if (!result.output.empty()) error += ": " + result.output;
    return error;
}

}  // namespace

std::string enable_applaunch_after_reboot(const std::string &user,
                                          unsigned int uid,
                                          const HandoffCommandRunner &run)
{
    const std::string uid_text = std::to_string(uid);
    const std::string runtime_dir = "XDG_RUNTIME_DIR=/run/user/" + uid_text;
    const std::string bus_address =
        "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/" + uid_text + "/bus";
    const std::vector<std::vector<std::string>> commands = {
        {"systemctl", "--global", "disable", "APPLaunch.service"},
        {"loginctl", "enable-linger", user},
        {"systemctl", "daemon-reload"},
        {"systemctl", "start", "user@" + uid_text + ".service"},
        {"runuser", "-u", user, "--", "env", runtime_dir, bus_address,
         "systemctl", "--user", "daemon-reload"},
        {"runuser", "-u", user, "--", "env", runtime_dir, bus_address,
         "systemctl", "--user", "enable", "APPLaunch.service"},
    };
    for (const auto &command : commands) {
        const HandoffCommandResult result = run(command);
        if (result.code != 0)
            return command_error(
                "Failed to enable APPLaunch.service for the configured user",
                result);
    }
    return {};
}

std::string handoff_to_applaunch(const std::string &user, unsigned int uid,
                                 const HandoffCommandRunner &run)
{
    const std::string uid_text = std::to_string(uid);
    const std::string runtime_dir = "XDG_RUNTIME_DIR=/run/user/" + uid_text;
    const std::string bus_address =
        "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/" + uid_text + "/bus";
    const std::vector<std::string> user_systemctl = {
        "runuser", "-u", user, "--", "env", runtime_dir, bus_address,
        "systemctl", "--user",
    };

    const auto required = [&run](const char *step,
                                 const std::vector<std::string> &args) {
        HandoffCommandResult result = run(args);
        return result.code == 0 ? std::string() : command_error(step, result);
    };

    std::string error = required(
        "Failed to remove global APPLaunch enablement",
        {"systemctl", "--global", "disable", "APPLaunch.service"});
    if (!error.empty()) return error;

    error = required(
        "Failed to enable the APPLaunch user manager",
        {"loginctl", "enable-linger", user});
    if (!error.empty()) return error;

    error = required("Failed to reload system services",
                     {"systemctl", "daemon-reload"});
    if (!error.empty()) return error;

    error = required("Failed to start the APPLaunch user manager",
                     {"systemctl", "start", "user@" + uid_text + ".service"});
    if (!error.empty()) return error;

    std::vector<std::string> args = user_systemctl;
    args.push_back("daemon-reload");
    error = required("Failed to reload APPLaunch user services", args);
    if (!error.empty()) return error;

    args = user_systemctl;
    args.insert(args.end(), {"enable", "--now", "APPLaunch.service"});
    error = required("Failed to enable and start APPLaunch.service", args);
    if (!error.empty()) return error;

    args = user_systemctl;
    args.insert(args.end(), {"is-active", "--quiet", "APPLaunch.service"});
    error = required("APPLaunch.service did not become active", args);
    if (!error.empty()) return error;

    return required("APPLaunch is active, but LaunchWizard could not be disabled",
                    {"systemctl", "disable", "--now", "LaunchWizard.service"});
}

}  // namespace launch_wizard
