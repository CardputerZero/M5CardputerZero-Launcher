#include "zclaw_cli_service.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeCommands {
public:
    std::vector<zclaw::CommandResult> results;
    std::vector<zclaw::CliService::Command> commands;
    std::size_t next_result = 0;

    zclaw::CommandResult run(const zclaw::CliService::Command &command)
    {
        commands.push_back(command);
        assert(next_result < results.size());
        return results[next_result++];
    }
};

zclaw::CliService make_service(FakeCommands *commands,
                               std::vector<unsigned int> *sleeps)
{
    return zclaw::CliService(
        [commands](const zclaw::CliService::Command &command) {
            return commands->run(command);
        },
        [sleeps](unsigned int seconds) { sleeps->push_back(seconds); });
}

bool ends_with(const zclaw::CliService::Command &command,
               const std::vector<std::string> &suffix)
{
    return command.size() >= suffix.size() &&
           std::equal(suffix.begin(), suffix.end(),
                      command.end() - static_cast<std::ptrdiff_t>(suffix.size()));
}

}  // namespace

int main()
{
    FakeCommands start_commands;
    start_commands.results = {{1, "already installed"}, {0, "started"}};
    std::vector<unsigned int> sleeps;
    std::string error;
    assert(make_service(&start_commands, &sleeps).start_service(&error));
    assert(start_commands.commands.size() == 2);
    assert(ends_with(start_commands.commands[0], {"service", "install"}));
    assert(ends_with(start_commands.commands[1], {"service", "start"}));
    assert(sleeps.empty());

    FakeCommands restart_commands;
    restart_commands.results = {{0, "restarted"}};
    assert(make_service(&restart_commands, &sleeps).restart_service(&error));
    assert(restart_commands.commands.size() == 1);
    assert(ends_with(restart_commands.commands[0], {"service", "restart"}));

    FakeCommands restart_failure;
    restart_failure.results = {{1, "service unavailable"}};
    assert(!make_service(&restart_failure, &sleeps).restart_service(&error));
    assert(error == "service restart failed:\nservice unavailable");

    FakeCommands install_failure;
    install_failure.results = {{1, "permission denied"}};
    assert(!make_service(&install_failure, &sleeps).start_service(&error));
    assert(install_failure.commands.size() == 1);
    assert(error == "service install failed:\npermission denied");

    FakeCommands start_failure;
    start_failure.results = {{0, "installed"}, {1, "service unavailable"}};
    assert(!make_service(&start_failure, &sleeps).start_service(&error));
    assert(start_failure.commands.size() == 2);
    assert(error == "service start failed:\nservice unavailable");

    FakeCommands pairing;
    pairing.results = {{1, "not ready"},
                       {0, "still no code"},
                       {0, "Pairing code: 123456"}};
    sleeps.clear();
    assert(make_service(&pairing, &sleeps).generate_pairing_code() == "123456");
    assert(pairing.commands.size() == 3);
    assert((sleeps == std::vector<unsigned int>{1, 1}));
    for (const zclaw::CliService::Command &command : pairing.commands)
        assert(ends_with(command, {"gateway", "get-paircode", "--new"}));

    FakeCommands exhausted;
    exhausted.results.resize(12, {1, "not ready"});
    sleeps.clear();
    assert(make_service(&exhausted, &sleeps).generate_pairing_code().empty());
    assert(exhausted.commands.size() == 12);
    assert(sleeps.size() == 11);
    for (unsigned int seconds : sleeps)
        assert(seconds == 1);

    FakeCommands unused;
    sleeps.clear();
    assert(!make_service(&unused, &sleeps)
                .apply_config(nullptr, zclaw::ProviderConfig{}, &error));
    assert(unused.commands.empty());
    assert(error == "Missing UI configuration.");
    return 0;
}
