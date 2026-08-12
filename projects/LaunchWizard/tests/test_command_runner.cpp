#include "command_runner.h"

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

bool expect(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "command runner: " << message << '\n';
    return condition;
}

launch_wizard::CommandOptions fast_options()
{
    launch_wizard::CommandOptions options;
    options.timeout = 300ms;
    options.terminate_grace = 100ms;
    return options;
}

pid_t output_pid(const std::string &output)
{
    char *end = nullptr;
    const long value = std::strtol(output.c_str(), &end, 10);
    return end == output.c_str() ? -1 : static_cast<pid_t>(value);
}

}  // namespace

bool test_command_runner()
{
    bool passed = true;

    auto options = fast_options();
    auto started = std::chrono::steady_clock::now();
    auto result = launch_wizard::run_command_process(
        {"/bin/sh", "-c", "while :; do sleep 10; done"}, nullptr, options);
    auto elapsed = std::chrono::steady_clock::now() - started;
    passed &= expect(result.timed_out && result.code == 124,
                     "a permanently blocked command must time out");
    passed &= expect(elapsed < 2s, "timeout must return promptly");
    passed &= expect(result.output.find("retry") != std::string::npos,
                     "timeout must explain recovery");

    options = fast_options();
    started = std::chrono::steady_clock::now();
    result = launch_wizard::run_command_process(
        {"/bin/sh", "-c", "trap '' TERM; echo $$; while :; do sleep 10; done"},
        nullptr, options);
    elapsed = std::chrono::steady_clock::now() - started;
    passed &= expect(result.timed_out && elapsed < 2s,
                     "a command that ignores TERM must be killed after grace");

    options = fast_options();
    options.timeout = 2s;
    started = std::chrono::steady_clock::now();
    result = launch_wizard::run_command_process(
        {"/bin/sh", "-c", "sleep 30 & echo $!; exit 0"}, nullptr, options);
    elapsed = std::chrono::steady_clock::now() - started;
    const pid_t descendant = output_pid(result.output);
    passed &= expect(result.code == 0 && elapsed < 1s,
                     "a descendant holding stdout must not hold the caller open");
    if (descendant > 0) {
        std::this_thread::sleep_for(30ms);
        passed &= expect(kill(descendant, 0) != 0 && errno == ESRCH,
                         "a pipe-holding descendant must be reclaimed");
    }

    options = fast_options();
    options.timeout = 2s;
    options.max_output_bytes = 1024;
    const std::string input(256 * 1024, 'i');
    result = launch_wizard::run_command_process(
        {"/bin/sh", "-c", "dd if=/dev/zero bs=4096 count=64 2>/dev/null; wc -c"},
        &input, options);
    passed &= expect(result.code == 0, "stdout and stdin must make progress concurrently");
    passed &= expect(result.output.size() <= 1044,
                     "captured output must remain bounded");

    std::atomic<bool> cancel{false};
    options = fast_options();
    options.timeout = 5s;
    options.cancelled = [&cancel]() { return cancel.load(); };
    std::thread cancel_thread([&cancel]() {
        std::this_thread::sleep_for(80ms);
        cancel.store(true);
    });
    result = launch_wizard::run_command_process(
        {"/bin/sh", "-c", "while :; do sleep 10; done"}, nullptr, options);
    cancel_thread.join();
    passed &= expect(result.was_cancelled && result.code == 125,
                     "cancellation must stop an in-flight command");
    passed &= expect(result.output.find("retry") != std::string::npos,
                     "cancellation must explain recovery");

    options = fast_options();
    options.timeout = 2s;
    const std::string unread_input(256 * 1024, 'x');
    result = launch_wizard::run_command_process(
        {"/bin/sh", "-c", "exit 7"}, &unread_input, options);
    passed &= expect(result.code == 7,
                     "early stdin closure must not terminate or block the caller");
    return passed;
}
