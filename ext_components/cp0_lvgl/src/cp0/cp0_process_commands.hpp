#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace cp0_process_commands {

int run_argv(const std::vector<std::string> &argv, bool background);
int run_sudo(const std::string &password, const std::vector<std::string> &argv);
int capture_argv(const std::vector<std::string> &argv, std::string &output);
int capture_argv_with_timeout(const std::vector<std::string> &argv,
                              std::string &output, int timeout_ms,
                              const std::atomic<bool> *cancel = nullptr);

// Replaces the child process image and only returns on unsupported platforms.
void exec_shell_as_configured_user(const char *command);

} // namespace cp0_process_commands
