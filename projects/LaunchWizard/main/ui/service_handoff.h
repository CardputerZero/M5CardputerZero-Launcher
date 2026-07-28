#ifndef LAUNCH_WIZARD_SERVICE_HANDOFF_H
#define LAUNCH_WIZARD_SERVICE_HANDOFF_H

#include <functional>
#include <string>
#include <vector>

namespace launch_wizard {

struct HandoffCommandResult {
    int code = 0;
    std::string output;
};

using HandoffCommandRunner =
    std::function<HandoffCommandResult(const std::vector<std::string> &)>;

// Enables APPLaunch for future user sessions without requiring a running user
// manager or D-Bus session during first-boot configuration.
std::string enable_applaunch_after_reboot(const HandoffCommandRunner &run);

// Starts and verifies APPLaunch before stopping LaunchWizard. On failure the
// remaining commands are not run, so the currently visible wizard stays up.
std::string handoff_to_applaunch(const std::string &user, unsigned int uid,
                                 const HandoffCommandRunner &run);

}  // namespace launch_wizard

#endif
