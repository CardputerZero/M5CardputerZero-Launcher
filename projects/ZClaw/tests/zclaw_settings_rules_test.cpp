#include "zclaw_provider_form_model.h"
#include "zclaw_panel_lifecycle.h"
#include "zclaw_settings_navigation_model.h"

#include <cassert>
#include <string>

int main()
{
    zclaw::PanelLifecycle panel;
    assert(!panel.mounted() && !panel.animating());
    assert(panel.mount());
    assert(!panel.mount() && panel.mounted());
    assert(panel.begin_open() && panel.animating());
    assert(!panel.begin_close());
    assert(!panel.complete_animation());
    assert(panel.mounted() && !panel.animating());
    assert(panel.begin_close() && panel.animating());
    assert(panel.complete_animation());
    assert(!panel.mounted() && !panel.animating());
    assert(panel.mount());
    assert(panel.begin_close());
    panel.release();
    assert(!panel.mounted() && !panel.complete_animation());

    using zclaw::SettingsActivationAction;
    using zclaw::SettingsBackAction;
    using zclaw::SettingsView;

    zclaw::ProviderConfig hosted = {"zclaw", "openai", "gpt", "https://api.example", ""};
    assert(zclaw::setup_edit_field_for_row(hosted, 1) == zclaw::SetupEditField::ApiKey);
    assert(zclaw::setup_edit_field_for_row(hosted, 2) == zclaw::SetupEditField::None);
    zclaw::SetupValidation validation = zclaw::validate_setup_provider(hosted);
    assert(!validation.valid && validation.row == 1);
    hosted.api_key = "key";
    assert(zclaw::validate_setup_provider(hosted).valid);

    zclaw::ProviderConfig custom = {"zclaw", "custom", "", "", ""};
    assert(zclaw::setup_edit_field_for_row(custom, 1) == zclaw::SetupEditField::Uri);
    assert(zclaw::setup_edit_field_for_row(custom, 2) == zclaw::SetupEditField::ApiKey);
    assert(zclaw::setup_edit_field_for_row(custom, 3) == zclaw::SetupEditField::Model);
    assert(zclaw::setup_edit_field_for_row(custom, 4) == zclaw::SetupEditField::WireApi);
    assert(custom.wire_api == "completions");
    zclaw::toggle_wire_api(&custom);
    assert(custom.wire_api == "responses");
    zclaw::toggle_wire_api(&custom);
    assert(custom.wire_api == "completions");
    validation = zclaw::validate_setup_provider(custom);
    assert(!validation.valid && validation.row == 2);
    custom.api_key = "key";
    custom.uri = "ftp://invalid";
    validation = zclaw::validate_setup_provider(custom);
    assert(!validation.valid && validation.row == 1);
    custom.uri = "https://example.com/v1";
    validation = zclaw::validate_setup_provider(custom);
    assert(!validation.valid && validation.row == 3);
    custom.model = "model";
    assert(zclaw::validate_setup_provider(custom).valid);

    const zclaw::ProviderConfig ollama = {"local", "ollama", "llama", "", ""};
    assert(zclaw::setup_edit_field_for_row(ollama, 1) == zclaw::SetupEditField::None);
    assert(zclaw::validate_setup_provider(ollama).valid);

    assert(zclaw::provider_edit_field_for_row(3) == zclaw::ProviderEditField::Uri);
    assert(zclaw::provider_edit_field_for_row(5) == zclaw::ProviderEditField::WireApi);
    std::string *editable_uri =
        zclaw::provider_field_value(&custom, zclaw::ProviderEditField::Uri);
    assert(editable_uri);
    *editable_uri = "https://changed.example/v1";
    assert(custom.uri == "https://changed.example/v1");
    assert(zclaw::provider_field_value(nullptr, zclaw::ProviderEditField::Uri) == nullptr);
    assert(zclaw::provider_field_value(&custom, zclaw::ProviderEditField::None) == nullptr);

    assert(zclaw::settings_back_action(SettingsView::Setup, true, false) ==
           SettingsBackAction::None);
    assert(zclaw::settings_back_action(SettingsView::Setup, false, true) ==
           SettingsBackAction::ShowSetupProviders);
    assert(zclaw::settings_back_action(SettingsView::Setup, false, false) ==
           SettingsBackAction::Close);
    assert(zclaw::settings_back_action(SettingsView::SetupProviders, false, true) ==
           SettingsBackAction::Quit);
    assert(zclaw::settings_back_action(SettingsView::SetupProviders, false, false) ==
           SettingsBackAction::ShowSetup);
    assert(zclaw::settings_back_action(SettingsView::Authorization, false, false) ==
           SettingsBackAction::Close);
    assert(zclaw::settings_back_action(SettingsView::Main, false, false) ==
           SettingsBackAction::Close);
    assert(zclaw::settings_back_action(SettingsView::ProviderDetail, false, false) ==
           SettingsBackAction::ShowProviders);
    assert(zclaw::settings_back_action(SettingsView::Providers, false, false) ==
           SettingsBackAction::ShowMain);

    zclaw::SettingsActivationContext context;
    context.view = SettingsView::Setup;
    context.setup_initialize_row = 2;
    assert(zclaw::settings_activation(context).action ==
           SettingsActivationAction::ShowSetupProviders);
    context.selected_row = 1;
    assert(zclaw::settings_activation(context).action ==
           SettingsActivationAction::EditSetupField);
    context.selected_row = 2;
    assert(zclaw::settings_activation(context).action == SettingsActivationAction::StartSetup);
    context.setup_in_flight = true;
    assert(zclaw::settings_activation(context).action == SettingsActivationAction::None);

    context = {};
    context.view = SettingsView::SetupProviders;
    assert(zclaw::settings_activation(context).action ==
           SettingsActivationAction::SelectSetupProvider);
    context.view = SettingsView::Authorization;
    assert(zclaw::settings_activation(context).action ==
           SettingsActivationAction::EnterPairingCode);
    context.selected_row = 4;
    assert(zclaw::settings_activation(context).action == SettingsActivationAction::ClearToken);
    context.selected_row = 2;
    assert(zclaw::settings_activation(context).action == SettingsActivationAction::None);

    context.view = SettingsView::Main;
    context.selected_row = 0;
    assert(zclaw::settings_activation(context).action == SettingsActivationAction::ShowSetup);
    context.selected_row = 1;
    assert(zclaw::settings_activation(context).action ==
           SettingsActivationAction::ShowAuthorization);
    context.selected_row = 2;
    assert(zclaw::settings_activation(context).action == SettingsActivationAction::ShowProviders);

    context.view = SettingsView::Providers;
    context.provider_count = 3;
    context.provider_scroll = 0;
    context.selected_row = 0;
    assert(zclaw::settings_activation(context).action == SettingsActivationAction::AddProvider);
    context.provider_scroll = 1;
    context.selected_row = 1;
    const zclaw::SettingsActivation provider_activation = zclaw::settings_activation(context);
    assert(provider_activation.action == SettingsActivationAction::ShowProviderDetail);
    assert(provider_activation.provider_index == 1);
    context.provider_scroll = 3;
    context.selected_row = 1;
    assert(zclaw::settings_activation(context).action == SettingsActivationAction::None);
    context.view = SettingsView::ProviderDetail;
    assert(zclaw::settings_activation(context).action ==
           SettingsActivationAction::EditProviderField);
    return 0;
}
