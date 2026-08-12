#ifndef LAUNCH_WIZARD_COMMAND_RUNNER_H
#define LAUNCH_WIZARD_COMMAND_RUNNER_H

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace launch_wizard {

struct CommandOptions {
    std::chrono::milliseconds timeout{30000};
    std::chrono::milliseconds terminate_grace{1000};
    size_t max_output_bytes = 4096;
    std::function<bool()> cancelled;
};

struct CommandResult {
    int code = 0;
    std::string output;
    bool timed_out = false;
    bool was_cancelled = false;
};

CommandResult run_command_process(const std::vector<std::string> &args,
                                  const std::string *stdin_text = nullptr,
                                  const CommandOptions &options = {});

}  // namespace launch_wizard

#endif
