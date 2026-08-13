#pragma once

namespace zclaw {

enum class SettingsView {
    Setup,
    SetupProviders,
    Authorization,
    Main,
    Providers,
    ProviderDetail,
};

enum class SettingsBackAction {
    None,
    Close,
    Quit,
    ShowMain,
    ShowSetup,
    ShowSetupProviders,
    ShowProviders,
};

enum class SettingsActivationAction {
    None,
    ShowSetup,
    ShowSetupProviders,
    ShowAuthorization,
    ShowProviders,
    ToggleUiSounds,
    SelectSetupProvider,
    StartSetup,
    EditSetupField,
    EnterPairingCode,
    ClearToken,
    AddProvider,
    ShowProviderDetail,
    EditProviderField,
};

struct SettingsActivationContext {
    SettingsView view = SettingsView::Main;
    int selected_row = 0;
    int setup_initialize_row = 0;
    int provider_scroll = 0;
    int provider_count = 0;
    bool setup_in_flight = false;
};

struct SettingsActivation {
    SettingsActivationAction action = SettingsActivationAction::None;
    int provider_index = -1;
};

SettingsBackAction settings_back_action(SettingsView view, bool setup_in_flight,
                                        bool first_run_needed);
SettingsActivation settings_activation(const SettingsActivationContext &context);

}  // namespace zclaw
