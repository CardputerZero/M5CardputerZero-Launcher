#include "zclaw_cli_config_model.h"

namespace zclaw {
namespace {

std::string zeroclaw_wire_api(const std::string &wire_api)
{
    return wire_api == "responses" ? "responses" : "chat_completions";
}

}  // namespace

CliConfigPlan make_cli_config_plan(const UiConfig &config,
                                   const ProviderConfig &provider)
{
    CliConfigPlan plan;
    plan.agent_alias = config.agent_alias.empty() ? "zclaw" : config.agent_alias;
    plan.webhook_url = "http://127.0.0.1:42617/webhook";

    const std::string provider_prefix =
        "providers.models." + provider.family + "." + provider.alias;
    const std::string provider_reference = provider.family + "." + provider.alias;
    plan.initial_settings = {
        {"gateway.host", "127.0.0.1"},
        {"gateway.port", "42617"},
        {"gateway.require_pairing", "true"},
        {"gateway.request_timeout_secs", "180"},
        {"gateway.long_running_request_timeout_secs", "600"},
        {provider_prefix + ".model", provider.model},
        {provider_prefix + ".uri", provider.uri},
        {provider_prefix + ".api_key", provider.api_key},
        {provider_prefix + ".wire_api", zeroclaw_wire_api(provider.wire_api)},
    };
    const std::string agent_prefix = "agents." + plan.agent_alias;
    plan.agent_settings = {
        {agent_prefix + ".enabled", "true"},
        {agent_prefix + ".model_provider", provider_reference},
        {agent_prefix + ".risk_profile", "default"},
        {"onboard_state.quickstart_completed", "true"},
    };
    return plan;
}

}  // namespace zclaw
