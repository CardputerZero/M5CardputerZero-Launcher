#pragma once

#include "zclaw_process_runner.h"
#include "zclaw_types.h"

#include <functional>
#include <string>
#include <vector>

namespace zclaw {

class CliService {
public:
    using Command = std::vector<std::string>;
    using CommandRunner = std::function<CommandResult(const Command &command)>;
    using SecretCommandRunner =
        std::function<CommandResult(const Command &command, const std::string &secret)>;
    using Sleeper = std::function<void(unsigned int seconds)>;

    CliService();
    CliService(CommandRunner command_runner, Sleeper sleeper,
               SecretCommandRunner secret_command_runner = {});

    bool apply_config(UiConfig *config, const ProviderConfig &provider,
                      std::string *error) const;
    bool start_service(std::string *error) const;
    bool restart_service(std::string *error) const;
    std::string generate_pairing_code() const;

private:
    bool config_set(const std::string &path, const std::string &value,
                    std::string *error) const;
    bool ensure_agent(const std::string &alias, std::string *error) const;

    CommandRunner command_runner_;
    SecretCommandRunner secret_command_runner_;
    Sleeper sleeper_;
};

}  // namespace zclaw
