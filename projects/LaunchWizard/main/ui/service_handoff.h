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

// Enables APPLaunch only for the configured account. The user manager is
// started explicitly so this does not create a global link for greeter users.
std::string enable_applaunch_after_reboot(const std::string &user,
                                          unsigned int uid,
                                          const HandoffCommandRunner &run);

// Starts and verifies APPLaunch before stopping LaunchWizard. On failure the
// remaining commands are not run, so the currently visible wizard stays up.
std::string handoff_to_applaunch(const std::string &user, unsigned int uid,
                                 const HandoffCommandRunner &run);

}  // namespace launch_wizard

#endif
