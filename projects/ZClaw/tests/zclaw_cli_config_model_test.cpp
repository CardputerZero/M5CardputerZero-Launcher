#include "zclaw_cli_config_model.h"

#include <cassert>

int main()
{
    zclaw::UiConfig config;
    config.agent_alias.clear();
    const zclaw::ProviderConfig provider = {
        "primary", "openai", "gpt-test", "https://api.example/v1", "secret"
    };
    const zclaw::CliConfigPlan plan = zclaw::make_cli_config_plan(config, provider);

    assert(plan.agent_alias == "zclaw");
    assert(plan.webhook_url == "http://127.0.0.1:42617/webhook");
    assert(plan.initial_settings.size() == 9);
    assert(plan.initial_settings[0].path == "gateway.host");
    assert(plan.initial_settings[0].value == "127.0.0.1");
    assert(plan.initial_settings[4].path ==
           "gateway.long_running_request_timeout_secs");
    assert(plan.initial_settings[4].value == "600");
    assert(plan.initial_settings[5].path ==
           "providers.models.openai.primary.model");
    assert(plan.initial_settings[5].value == "gpt-test");
    assert(plan.initial_settings[6].path ==
           "providers.models.openai.primary.uri");
    assert(plan.initial_settings[6].value == "https://api.example/v1");
    assert(plan.initial_settings[7].path ==
           "providers.models.openai.primary.api_key");
    assert(plan.initial_settings[7].value == "secret");
    assert(plan.initial_settings[8].path ==
           "providers.models.openai.primary.wire_api");
    assert(plan.initial_settings[8].value == "chat_completions");

    zclaw::ProviderConfig responses_provider = provider;
    responses_provider.wire_api = "responses";
    const zclaw::CliConfigPlan responses_plan =
        zclaw::make_cli_config_plan(config, responses_provider);
    assert(responses_plan.initial_settings[8].value == "responses");

    assert(plan.agent_settings.size() == 4);
    assert(plan.agent_settings[0].path == "agents.zclaw.enabled");
    assert(plan.agent_settings[1].path == "agents.zclaw.model_provider");
    assert(plan.agent_settings[1].value == "openai.primary");
    assert(plan.agent_settings[2].path == "agents.zclaw.risk_profile");
    assert(plan.agent_settings[3].path == "onboard_state.quickstart_completed");

    config.agent_alias = "custom-agent";
    const zclaw::CliConfigPlan custom_agent_plan =
        zclaw::make_cli_config_plan(config, provider);
    assert(custom_agent_plan.agent_alias == "custom-agent");
    assert(custom_agent_plan.agent_settings[0].path ==
           "agents.custom-agent.enabled");
    return 0;
}
