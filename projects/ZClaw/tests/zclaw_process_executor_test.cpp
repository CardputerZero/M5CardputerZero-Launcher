#include "zclaw_process_executor.h"

#include <cassert>
#include <chrono>
#include <future>
#include <filesystem>
#include <string>

namespace {

std::size_t open_fd_count()
{
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        static_cast<void>(entry);
        ++count;
    }
    return count;
}

}  // namespace

int main()
{
    zclaw::ProcessExecutor executor;
    const zclaw::CommandResult output =
        executor.run({"printf", "  process output  \\n"});
    assert(output.ok());
    assert(output.output == "process output");

    const std::string literal = "space ; $(printf injected) ' quote";
    const zclaw::CommandResult literal_output = executor.run({"printf", literal});
    assert(literal_output.ok());
    assert(literal_output.output == literal);

    const std::string secret = "zclaw-secret-sentinel-7c52f9";
    const zclaw::CommandResult secret_output = executor.run_with_secret_input(
        {"/bin/sh", "-c",
         "tr '\\0' ' ' </proc/self/cmdline; printf '\\n'; "
         "IFS= read -r value; test -n \"$value\"; printf accepted"},
        secret);
    assert(secret_output.ok());
    assert(secret_output.output.find("accepted") != std::string::npos);
    assert(secret_output.output.find(secret) == std::string::npos);

    zclaw::ProcessExecutor deadline_executor;
    const auto deadline_started = std::chrono::steady_clock::now();
    const zclaw::CommandResult timed_out = deadline_executor.run_with_secret_input(
        {"/bin/sh", "-c", "IFS= read -r value; sleep 30"}, secret, 100);
    assert(!timed_out.ok());
    assert(timed_out.output == "command timed out");
    assert(std::chrono::steady_clock::now() - deadline_started <
           std::chrono::seconds(2));

    const zclaw::CommandResult bounded_output =
        deadline_executor.run_with_secret_input(
            {"/bin/sh", "-c",
             "IFS= read -r value; head -c 70000 /dev/zero | tr '\\0' x"},
            secret, 2000);
    assert(bounded_output.ok());
    assert(bounded_output.output.size() == 64 * 1024);

    const std::size_t descriptors_before = open_fd_count();
    for (int attempt = 0; attempt < 50; ++attempt)
        assert(executor.run({"/definitely/missing/zclaw-command"}).status != 0);
    assert(open_fd_count() == descriptors_before);

    std::future<zclaw::CommandResult> blocked =
        std::async(std::launch::async, [&executor] {
            return executor.run({"/bin/sh", "-c", "sleep 30; printf late"});
        });
    assert(blocked.wait_for(std::chrono::milliseconds(20)) ==
           std::future_status::timeout);
    executor.shutdown();
    assert(blocked.wait_for(std::chrono::seconds(1)) ==
           std::future_status::ready);
    assert(!blocked.get().ok());
    assert(executor.shutdown_requested());
    executor.shutdown();

    const zclaw::CommandResult rejected = executor.run({"printf", "unexpected"});
    assert(!rejected.ok());
    assert(rejected.output == "command cancelled");
    assert(!executor.run({}).ok());

    const auto wait_started = std::chrono::steady_clock::now();
    executor.wait(30);
    assert(std::chrono::steady_clock::now() - wait_started <
           std::chrono::seconds(1));

    return 0;
}
