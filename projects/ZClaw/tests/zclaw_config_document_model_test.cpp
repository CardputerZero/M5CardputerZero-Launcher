#include "zclaw_config_document_model.h"

#include <cassert>
#include <vector>

namespace {

bool same_provider(const zclaw::ProviderConfig &left,
                   const zclaw::ProviderConfig &right)
{
    return left.alias == right.alias && left.family == right.family &&
           left.model == right.model && left.uri == right.uri &&
           left.api_key == right.api_key && left.wire_api == right.wire_api;
}

}  // namespace

int main()
{
    std::vector<zclaw::ProviderConfig> checked_providers;
    const std::vector<zclaw::ProviderConfig> providers = {
        {"primary", "custom", "model\\name\nnext",
         "https://example.com/a\tb", "key\\value"},
        {"local", "ollama", "llama", "http://127.0.0.1:11434", ""},
    };
    const std::string provider_document =
        zclaw::make_provider_config_document(providers);
    const std::vector<zclaw::ProviderConfig> parsed_providers =
        zclaw::parse_provider_config_document(
            "malformed\n\n" + provider_document + "too\tfew\tfields\n");
    assert(parsed_providers.size() == providers.size());
    assert(same_provider(parsed_providers[0], providers[0]));
    assert(same_provider(parsed_providers[1], providers[1]));
    assert(zclaw::make_provider_config_document({}).empty());
    const std::vector<zclaw::ProviderConfig> legacy =
        zclaw::parse_provider_config_document("old\tcustom\tmodel\thttps://api\tkey\n");
    assert(legacy.size() == 1);
    assert(legacy[0].wire_api == "completions");
    assert(!zclaw::parse_provider_config_document_checked(
        "alias\tcustom\tmodel\turi\tkey\tbad\\q\n", &checked_providers));

    zclaw::UiConfig config;
    config.webhook_url = "https://example.com/hook\tpath";
    config.agent_alias = "test\nagent";
    config.webhook_secret = "hook\\secret";
    config.bearer_token = "token";
    config.setup_complete = true;
    const zclaw::UiConfig parsed_config = zclaw::parse_ui_config_document(
        "ignored\nunknown\tvalue\n" + zclaw::make_ui_config_document(config));
    assert(parsed_config.webhook_url == config.webhook_url);
    assert(parsed_config.agent_alias == config.agent_alias);
    assert(parsed_config.webhook_secret == config.webhook_secret);
    assert(parsed_config.bearer_token == config.bearer_token);
    assert(parsed_config.setup_complete);

    const zclaw::UiConfig defaults =
        zclaw::parse_ui_config_document("agent_alias\t\nsetup_complete\ttrue\n");
    assert(defaults.agent_alias == "zclaw");
    assert(!defaults.setup_complete);

    assert(!zclaw::parse_provider_config_document_checked(
        "alias\\q\tfamily\tmodel\turi\tkey\n", &checked_providers));
    assert(zclaw::parse_provider_config_document_checked(
        "bad\\q\tfamily\tmodel\turi\tkey\n" + provider_document,
        &checked_providers));
    assert(checked_providers.size() == providers.size());
    const std::vector<zclaw::ProviderConfig> lenient_providers =
        zclaw::parse_provider_config_document(
            "alias\\q\tfamily\tmodel\turi\tkey\n");
    assert(lenient_providers.size() == 1);
    assert(lenient_providers[0].alias == "aliasq");

    zclaw::UiConfig checked_config;
    assert(!zclaw::parse_ui_config_document_checked(
        "setup_complete\ttrue\n", &checked_config));
    assert(!zclaw::parse_ui_config_document_checked(
        "agent_alias\tfirst\nagent_alias\tsecond\n", &checked_config));
    assert(!zclaw::parse_ui_config_document_checked(
        "agent_alias\tbad\\qvalue\n", &checked_config));
    const zclaw::UiConfig lenient_config = zclaw::parse_ui_config_document(
        "agent_alias\tbad\\qvalue\nagent_alias\tsecond\n");
    assert(lenient_config.agent_alias == "second");
    assert(zclaw::parse_ui_config_document_checked(
        "unknown\tvalue\\qfuture\nagent_alias\tvalid\nsetup_complete\t0\n",
        &checked_config));
    assert(checked_config.agent_alias == "valid");
    assert(!checked_config.setup_complete);
    return 0;
}
