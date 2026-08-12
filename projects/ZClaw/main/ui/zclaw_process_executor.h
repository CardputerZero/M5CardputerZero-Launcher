#pragma once

#include "zclaw_process_runner.h"

#include <condition_variable>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <sys/types.h>

namespace zclaw {

class ProcessExecutor {
public:
    ProcessExecutor() = default;
    ~ProcessExecutor();

    ProcessExecutor(const ProcessExecutor &) = delete;
    ProcessExecutor &operator=(const ProcessExecutor &) = delete;

    CommandResult run(const std::vector<std::string> &arguments);
    CommandResult run_with_secret_input(const std::vector<std::string> &arguments,
                                        const std::string &secret,
                                        int timeout_ms = 30000);
    void wait(unsigned int seconds) const;
    void shutdown() noexcept;
    bool shutdown_requested() const;

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::set<pid_t> active_processes_;
    bool shutdown_ = false;
};

}  // namespace zclaw
