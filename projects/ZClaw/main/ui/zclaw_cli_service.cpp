#include "zclaw_cli_service.h"

#include "zclaw_cli_config_model.h"
#include "zclaw_cli_output_model.h"
#include "zclaw_paths.h"
#include "zclaw_process_executor.h"
#include "zclaw_quickstart_model.h"
#include "zclaw_risk_profile_store.h"

#include <unistd.h>

#include <initializer_list>
#include <utility>

namespace zclaw {
namespace {

CliService::Command zeroclaw_command(std::initializer_list<const char *> arguments)
{
    CliService::Command command = {paths::zeroclaw_binary()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return command;
}

}  // namespace

CliService::CliService()
    : CliService([](const Command &command) {
                     ProcessExecutor executor;
                     return executor.run(command);
                 },
                 [](unsigned int seconds) { ::sleep(seconds); })
{
}

CliService::CliService(CommandRunner command_runner, Sleeper sleeper)
    : command_runner_(std::move(command_runner)), sleeper_(std::move(sleeper))
{
}

bool CliService::apply_config(UiConfig *config, const ProviderConfig &provider,
                              std::string *error) const
{
    if (!config) {
        if (error)
            *error = "Missing UI configuration.";
        return false;
    }
    const CliConfigPlan plan = make_cli_config_plan(*config, provider);
    for (const CliConfigEntry &setting : plan.initial_settings) {
        if (!config_set(setting.path, setting.value, error))
            return false;
    }

    if (!ensure_agent(plan.agent_alias, error) ||
        !RiskProfileStore().ensure_default(paths::zeroclaw_config(), error))
        return false;
    for (const CliConfigEntry &setting : plan.agent_settings) {
        if (!config_set(setting.path, setting.value, error))
            return false;
    }

    config->agent_alias = plan.agent_alias;
    config->webhook_url = plan.webhook_url;
    return true;
}

bool CliService::start_service(std::string *error) const
{
    CommandResult result = command_runner_(zeroclaw_command({"service", "install"}));
    if (!cli_service_command_succeeded(result.ok(), result.output,
                                       CliServiceCommand::Install)) {
        if (error)
            *error = "service install failed:\n" + result.output;
        return false;
    }

    result = command_runner_(zeroclaw_command({"service", "start"}));
    if (!cli_service_command_succeeded(result.ok(), result.output,
                                       CliServiceCommand::Start)) {
        if (error)
            *error = "service start failed:\n" + result.output;
        return false;
    }
    return true;
}

bool CliService::restart_service(std::string *error) const
{
    const CommandResult result =
        command_runner_(zeroclaw_command({"service", "restart"}));
    if (result.ok())
        return true;
    if (error)
        *error = "service restart failed:\n" + result.output;
    return false;
}

std::string CliService::generate_pairing_code() const
{
    for (int attempt = 0; attempt < 12; ++attempt) {
        const CommandResult result = command_runner_(
            zeroclaw_command({"gateway", "get-paircode", "--new"}));
        if (result.ok()) {
            const std::string code = extract_pairing_code(result.output);
            if (!code.empty())
                return code;
        }
        if (attempt + 1 < 12 && sleeper_)
            sleeper_(1);
    }
    return "";
}

bool CliService::config_set(const std::string &path, const std::string &value,
                            std::string *error) const
{
    Command command = zeroclaw_command({"config", "set", "--no-interactive"});
    command.push_back(path);
    command.push_back(value);
    const CommandResult result = command_runner_(command);
    if (result.ok())
        return true;
    if (error)
        *error = "config set failed: " + path + "\n" + result.output;
    return false;
}

bool CliService::ensure_agent(const std::string &alias, std::string *error) const
{
    CommandResult result = command_runner_(zeroclaw_command({"agents", "list"}));
    if (!result.ok()) {
        if (error)
            *error = "agents list failed\n" + result.output;
        return false;
    }
    if (cli_agent_list_contains(result.output, alias))
        return true;
    Command command = zeroclaw_command({"agents", "create"});
    command.push_back(alias);
    result = command_runner_(command);
    if (!result.ok()) {
        if (error)
            *error = "agent creation failed\n" + result.output;
        return false;
    }
    return true;
}

}  // namespace zclaw
