#include "zclaw_settings_navigation_model.h"

namespace zclaw {

SettingsBackAction settings_back_action(SettingsView view, bool setup_in_flight,
                                        bool first_run_needed)
{
    if (setup_in_flight && view == SettingsView::Setup)
        return SettingsBackAction::None;
    switch (view) {
    case SettingsView::SetupProviders:
        return first_run_needed ? SettingsBackAction::Quit : SettingsBackAction::ShowSetup;
    case SettingsView::Setup:
        return first_run_needed ? SettingsBackAction::ShowSetupProviders :
                                  SettingsBackAction::Close;
    case SettingsView::Authorization:
    case SettingsView::Main:
        return SettingsBackAction::Close;
    case SettingsView::ProviderDetail:
        return SettingsBackAction::ShowProviders;
    case SettingsView::Providers:
        return SettingsBackAction::ShowMain;
    }
    return SettingsBackAction::None;
}

SettingsActivation settings_activation(const SettingsActivationContext &context)
{
    switch (context.view) {
    case SettingsView::Setup:
        if (context.setup_in_flight)
            return {};
        if (context.selected_row == 0)
            return {SettingsActivationAction::ShowSetupProviders};
        if (context.selected_row == context.setup_initialize_row)
            return {SettingsActivationAction::StartSetup};
        return {SettingsActivationAction::EditSetupField};
    case SettingsView::SetupProviders:
        return {SettingsActivationAction::SelectSetupProvider};
    case SettingsView::Authorization:
        if (context.selected_row == 0)
            return {SettingsActivationAction::EnterPairingCode};
        if (context.selected_row == 4)
            return {SettingsActivationAction::ClearToken};
        return {};
    case SettingsView::Main:
        if (context.selected_row == 0)
            return {SettingsActivationAction::ShowSetup};
        if (context.selected_row == 1)
            return {SettingsActivationAction::ShowAuthorization};
        if (context.selected_row == 2)
            return {SettingsActivationAction::ShowProviders};
        if (context.selected_row == 5)
            return {SettingsActivationAction::ToggleUiSounds};
        return {};
    case SettingsView::Providers: {
        const int item = context.provider_scroll + context.selected_row;
        if (item == 0)
            return {SettingsActivationAction::AddProvider};
        if (item > 0 && item <= context.provider_count)
            return {SettingsActivationAction::ShowProviderDetail, item - 1};
        return {};
    }
    case SettingsView::ProviderDetail:
        return {SettingsActivationAction::EditProviderField};
    }
    return {};
}

}  // namespace zclaw
