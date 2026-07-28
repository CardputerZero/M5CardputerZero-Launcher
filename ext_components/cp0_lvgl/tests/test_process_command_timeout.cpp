#include "../src/cp0/cp0_process_commands.hpp"
#include "cp0_bounded_task_registry.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

int main()
{
    std::string output;
    std::atomic<int> timeout_result{0};
    Cp0BoundedTaskRegistry tasks;
    const auto started = std::chrono::steady_clock::now();
    assert(tasks.start([&] {
        std::string ignored;
        timeout_result.store(cp0_process_commands::capture_argv_with_timeout(
            {"sh", "-c", "trap '' TERM; while :; do sleep 1; done"}, ignored, 150));
    }));
    tasks.join_all();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    assert(timeout_result.load() < 0);
    assert(tasks.tracked() == 0);
    assert(elapsed < std::chrono::seconds(2));

    output.clear();
    assert(cp0_process_commands::capture_argv_with_timeout(
               {"sh", "-c", "printf 'classified error' >&2; exit 7"}, output, 1000) == 7);
    assert(output == "classified error");

    std::atomic<bool> cancel{false};
    std::thread request_cancel([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cancel.store(true);
    });
    const auto cancel_started = std::chrono::steady_clock::now();
    assert(cp0_process_commands::capture_argv_with_timeout(
               {"sh", "-c", "trap '' TERM; while :; do sleep 1; done"},
               output, 30000, &cancel) == -ECANCELED);
    request_cancel.join();
    assert(std::chrono::steady_clock::now() - cancel_started < std::chrono::seconds(2));
}
