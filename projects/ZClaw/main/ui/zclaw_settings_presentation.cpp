#include "zclaw_settings_presentation.h"

#include "zclaw_provider_catalog.h"

#include <algorithm>

namespace zclaw {

SettingsPresentation present_settings_main(const UiConfig &config)
{
    return {
        "ZClaw Settings",
        "Tab / Esc",
        {
            {"Setup", config.setup_complete ? "Done" : "Run"},
            {"Authorization", config.bearer_token.empty() ? "Pair" : "Paired"},
            {"Providers", "Manage"},
            {"Agent", config.agent_alias},
            {"Transport", config.bearer_token.empty() ? "Webhook" : "WS"},
            {"UI Sounds", config.ui_sounds_enabled ? "On" : "Off"},
        },
    };
}

SettingsPresentation present_authorization(const UiConfig &config)
{
    return {
        "Authorization",
        "Enter / Esc",
        {
            {"Pair Code", "Enter"},
            {"Token", config.bearer_token.empty() ? "Missing" : "Saved"},
            {"Agent", config.agent_alias},
            {"Approvals", config.bearer_token.empty() ? "Off" : "WS"},
            {"Clear Token", "-"},
        },
    };
}

SettingsPresentation present_setup(const ProviderConfig &provider)
{
    SettingsPresentation presentation{"Model Settings", "Enter / Esc", {}};
    presentation.rows.push_back({"Provider", provider_display_name(provider.family)});
    if (provider.family == "custom")
        presentation.rows.push_back({"API URL", provider.uri.empty() ? "Required" : "Set"});
    if (provider.family != "ollama")
        presentation.rows.push_back({"API Key", provider.api_key.empty() ? "Required" : "Set"});
    if (provider.family == "custom")
        presentation.rows.push_back({"API Model", provider.model.empty() ? "Required" : "Set"});
    if (provider.family == "custom")
        presentation.rows.push_back({"API Type", provider.wire_api});
    presentation.rows.push_back({"Confirm", "Quickstart"});
    return presentation;
}

SettingsPresentation present_setup_providers(const PagedSelection &selection,
                                             int maximum_rows)
{
    SettingsPresentation presentation{"Model Provider", "Enter / Esc", {}};
    const int preset_count = static_cast<int>(provider_preset_count());
    const int start = std::max(0, selection.scroll_offset);
    const int end = std::min(preset_count, start + std::max(0, maximum_rows));
    for (int item = start; item < end; ++item) {
        const ProviderConfig preset = provider_preset(static_cast<std::size_t>(item));
        presentation.rows.push_back({provider_display_name(preset.family),
                                     item == selection.selected_index ? "Selected" : ""});
    }
    return presentation;
}

SettingsPresentation present_providers(const std::vector<ProviderConfig> &providers,
                                       const PagedSelection &selection,
                                       int maximum_rows)
{
    SettingsPresentation presentation{"Providers", "Enter / Esc", {}};
    const int total_rows = static_cast<int>(providers.size()) + 1;
    const int start = std::max(0, selection.scroll_offset);
    const int end = std::min(total_rows, start + std::max(0, maximum_rows));
    for (int item = start; item < end; ++item) {
        if (item == 0)
            presentation.rows.push_back({"Add Provider", "+"});
        else
            presentation.rows.push_back({providers[item - 1].alias,
                                         providers[item - 1].model});
    }
    return presentation;
}

SettingsPresentation present_provider_detail(const ProviderConfig &provider)
{
    SettingsPresentation presentation = {
        "Provider", "Enter / Del", {
            {"Alias", provider.alias},
            {"Family", provider.family},
            {"Model", provider.model},
            {"URI", provider.uri},
            {"API Key", provider.api_key.empty() ? "(empty)" : "set"},
        }};
    if (provider.family == "custom")
        presentation.rows.push_back({"API Type", provider.wire_api});
    return presentation;
}

SettingsPresentation present_setup_progress()
{
    return {"Quickstart", "Please wait", {}};
}

}  // namespace zclaw
