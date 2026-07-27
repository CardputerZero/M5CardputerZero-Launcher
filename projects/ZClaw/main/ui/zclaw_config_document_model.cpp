#include "zclaw_config_document_model.h"

#include "zclaw_config_codec.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace zclaw {
namespace {

bool parse_provider_document(const std::string &document,
                             std::vector<ProviderConfig> *providers,
                             bool strict)
{
    if (!providers)
        return false;
    providers->clear();
    std::istringstream lines(document);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty())
            continue;
        const std::vector<std::string> fields = split_config_line(line);
        if (fields.size() < 5)
            continue;
        bool valid_encoding = true;
        const std::size_t field_count = std::min<std::size_t>(fields.size(), 6);
        for (std::size_t index = 0; strict && index < field_count; ++index)
            valid_encoding = valid_encoding &&
                             is_valid_config_field_encoding(fields[index]);
        if (!valid_encoding)
            continue;
        ProviderConfig provider = {
            decode_config_field(fields[0]), decode_config_field(fields[1]),
            decode_config_field(fields[2]), decode_config_field(fields[3]),
            decode_config_field(fields[4]),
        };
        if (fields.size() >= 6)
            provider.wire_api = decode_config_field(fields[5]);
        providers->push_back(std::move(provider));
    }
    return !providers->empty();
}

bool parse_ui_document(const std::string &document, UiConfig *config,
                       bool strict)
{
    if (!config)
        return false;
    *config = UiConfig{};
    bool recognized = false;
    bool invalid = false;
    std::set<std::string> seen_keys;
    std::istringstream lines(document);
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<std::string> fields = split_config_line(line);
        if (fields.size() < 2)
            continue;
        if (strict && !is_valid_config_field_encoding(fields[0]))
            continue;
        const std::string key = decode_config_field(fields[0]);
        const bool known = key == "webhook_url" || key == "agent_alias" ||
                           key == "webhook_secret" || key == "bearer_token" ||
                           key == "setup_complete";
        if (!known)
            continue;
        if (strict && !is_valid_config_field_encoding(fields[1])) {
            invalid = true;
            continue;
        }
        const std::string value = decode_config_field(fields[1]);
        if (strict && !seen_keys.insert(key).second) {
            invalid = true;
            continue;
        }
        if (key == "webhook_url") {
            config->webhook_url = value;
            recognized = true;
        } else if (key == "agent_alias") {
            config->agent_alias = value.empty() ? "zclaw" : value;
            recognized = true;
        } else if (key == "webhook_secret") {
            config->webhook_secret = value;
            recognized = true;
        } else if (key == "bearer_token") {
            config->bearer_token = value;
            recognized = true;
        } else if (key == "setup_complete") {
            if (strict && value != "0" && value != "1") {
                invalid = true;
                continue;
            }
            config->setup_complete = value == "1";
            recognized = true;
        }
    }
    return recognized && !invalid;
}

}  // namespace

std::vector<ProviderConfig> parse_provider_config_document(
    const std::string &document)
{
    std::vector<ProviderConfig> providers;
    parse_provider_document(document, &providers, false);
    return providers;
}

bool parse_provider_config_document_checked(
    const std::string &document, std::vector<ProviderConfig> *providers)
{
    return parse_provider_document(document, providers, true);
}

std::string make_provider_config_document(
    const std::vector<ProviderConfig> &providers)
{
    std::string document;
    for (const ProviderConfig &provider : providers) {
        document += encode_config_field(provider.alias) + '\t' +
                    encode_config_field(provider.family) + '\t' +
                    encode_config_field(provider.model) + '\t' +
                    encode_config_field(provider.uri) + '\t' +
                    encode_config_field(provider.api_key) + '\t' +
                    encode_config_field(provider.wire_api) + '\n';
    }
    return document;
}

UiConfig parse_ui_config_document(const std::string &document)
{
    UiConfig config;
    parse_ui_document(document, &config, false);
    return config;
}

bool parse_ui_config_document_checked(const std::string &document,
                                      UiConfig *config)
{
    return parse_ui_document(document, config, true);
}

std::string make_ui_config_document(const UiConfig &config)
{
    std::string document;
    document += encode_config_field("webhook_url") + '\t' +
                encode_config_field(config.webhook_url) + '\n';
    document += encode_config_field("agent_alias") + '\t' +
                encode_config_field(config.agent_alias) + '\n';
    document += encode_config_field("webhook_secret") + '\t' +
                encode_config_field(config.webhook_secret) + '\n';
    document += encode_config_field("bearer_token") + '\t' +
                encode_config_field(config.bearer_token) + '\n';
    document += encode_config_field("setup_complete") + '\t' +
                (config.setup_complete ? "1" : "0") + '\n';
    return document;
}

}  // namespace zclaw
