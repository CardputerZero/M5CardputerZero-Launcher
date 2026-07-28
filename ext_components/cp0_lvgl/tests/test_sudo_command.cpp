#include "cp0_sudo_command.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

int main()
{
    const auto invalidation = cp0_sudo::invalidation_command();
    assert((invalidation == std::vector<std::string>{
        "/usr/bin/sudo", "-k",
    }));

    const auto validation = cp0_sudo::validation_command();
    assert((validation == std::vector<std::string>{
        "/usr/bin/sudo", "-S", "-v",
    }));

    const std::vector<std::string> arguments = {"/usr/bin/fixed-helper", "install"};
    const auto execution = cp0_sudo::execution_command(arguments, false, "/bin/sh");
    assert((execution == std::vector<std::string>{
        "/usr/bin/sudo", "-S", "--",
        "/usr/bin/fixed-helper", "install",
    }));
    assert(std::find(execution.begin(), execution.end(), "-n") == execution.end());

    const auto shell = cp0_sudo::execution_command({"fixed command"}, true, "/bin/bash");
    assert((shell == std::vector<std::string>{
        "/usr/bin/sudo", "-S", "--",
        "/bin/bash", "-c", "fixed command",
    }));
}
