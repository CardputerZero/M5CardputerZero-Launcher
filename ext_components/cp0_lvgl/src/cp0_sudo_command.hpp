#pragma once

#include <string>
#include <vector>

namespace cp0_sudo {

inline std::vector<std::string> invalidation_command()
{
    return {"/usr/bin/sudo", "-k"};
}

inline std::vector<std::string> validation_command()
{
    return {"/usr/bin/sudo", "-S", "-v"};
}

inline std::vector<std::string> execution_command(const std::vector<std::string> &arguments,
                                                  bool use_login_shell,
                                                  const std::string &shell)
{
    std::vector<std::string> command = {"/usr/bin/sudo", "-S", "--"};
    if (use_login_shell) {
        command.push_back(shell);
        command.push_back("-c");
        command.push_back(arguments.front());
    } else {
        command.insert(command.end(), arguments.begin(), arguments.end());
    }
    return command;
}

} // namespace cp0_sudo
