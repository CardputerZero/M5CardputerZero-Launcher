#include "zclaw_settings_workflow.h"

#include "zclaw_provider_manager.h"
#include "zclaw_settings_coordinator.h"
#include "zclaw_sound_effects.h"
#include "zclaw_ui_config_manager.h"

namespace zclaw {

SettingsWorkflow::SettingsWorkflow(ProviderManager &providers,
                                   UiConfigManager &config,
                                   SettingsCoordinator &settings,
                                   InputDialog &input, FontManager &fonts,
                                   ChatView &chat, AsyncService &async_service,
                                   SoundEffects &sounds)
    : providers_(providers), config_(config), settings_(settings), sounds_(sounds),
      authorization_(config, settings, input, fonts, chat, async_service),
      provider_workflow_(providers, settings, input, fonts, chat),
      setup_(providers, config, settings, input, fonts, chat, async_service)
{
}

bool SettingsWorkflow::setup_in_flight() const
{
    return setup_.in_flight();
}

bool SettingsWorkflow::setup_retry_pending() const
{
    return setup_.retry_pending();
}

void SettingsWorkflow::move_setup_retry_selection(int delta)
{
    setup_.move_retry_selection(delta);
}

bool SettingsWorkflow::activate_setup_retry_selection()
{
    return setup_.activate_retry_selection();
}

void SettingsWorkflow::dismiss_setup_retry()
{
    setup_.dismiss_error();
}

void SettingsWorkflow::open_setup(lv_obj_t *parent, bool first_run_needed)
{
    setup_.open(parent, first_run_needed);
}

void SettingsWorkflow::update_setup_progress(const SetupProgress &progress)
{
    setup_.update_progress(progress);
}

void SettingsWorkflow::apply_setup_edit(const std::string &value)
{
    setup_.apply_edit(value);
}

void SettingsWorkflow::apply_provider_edit(const std::string &value)
{
    provider_workflow_.apply_edit(value);
}

void SettingsWorkflow::start_pairing(const std::string &code)
{
    authorization_.start_pairing(code);
}

void SettingsWorkflow::delete_current_provider()
{
    provider_workflow_.delete_current();
}

void SettingsWorkflow::activate_selection()
{
    if (!settings_.is_open())
        return;
    if (setup_.retry_pending()) {
        setup_.start();
        return;
    }
    const SettingsActivation activation = settings_.state().activation(
        settings_.setup_initialize_row(),
        static_cast<int>(providers_.providers().size()), setup_.in_flight());
    switch (activation.action) {
    case SettingsActivationAction::None:
        break;
    case SettingsActivationAction::ShowSetup:
        setup_.render();
        break;
    case SettingsActivationAction::ShowSetupProviders:
        settings_.sync_setup_provider_selection();
        settings_.render_setup_providers();
        break;
    case SettingsActivationAction::ShowAuthorization:
        settings_.render_authorization();
        break;
    case SettingsActivationAction::ShowProviders:
        settings_.state().reset_providers();
        settings_.render_providers();
        break;
    case SettingsActivationAction::ToggleUiSounds: {
        UiConfig candidate = config_.config();
        candidate.ui_sounds_enabled = !candidate.ui_sounds_enabled;
        std::string error;
        if (!config_.update(candidate, &error))
            break;
        sounds_.set_enabled(candidate.ui_sounds_enabled);
        settings_.render_main();
        break;
    }
    case SettingsActivationAction::SelectSetupProvider:
        setup_.select_provider(
            settings_.state().setup_provider_selection().selected_index);
        break;
    case SettingsActivationAction::StartSetup:
        setup_.start();
        break;
    case SettingsActivationAction::EditSetupField:
        setup_.edit_selected_field();
        break;
    case SettingsActivationAction::EnterPairingCode:
        authorization_.open_pairing_input();
        break;
    case SettingsActivationAction::ClearToken:
        authorization_.clear_token();
        break;
    case SettingsActivationAction::AddProvider:
        provider_workflow_.add();
        break;
    case SettingsActivationAction::ShowProviderDetail:
        provider_workflow_.open_detail(activation.provider_index);
        break;
    case SettingsActivationAction::EditProviderField:
        provider_workflow_.edit_selected_field();
        break;
    }
}

bool SettingsWorkflow::navigate_back(bool first_run_needed)
{
    if (setup_.retry_pending()) {
        setup_.dismiss_error();
        return false;
    }
    switch (settings_.state().back_action(setup_.in_flight(), first_run_needed)) {
    case SettingsBackAction::None:
        break;
    case SettingsBackAction::Close:
        settings_.close();
        break;
    case SettingsBackAction::Quit:
        return true;
    case SettingsBackAction::ShowMain:
        settings_.state().set_selected_row(0);
        settings_.render_main();
        break;
    case SettingsBackAction::ShowSetup:
        settings_.state().set_selected_row(0);
        setup_.render();
        break;
    case SettingsBackAction::ShowSetupProviders:
        settings_.render_setup_providers();
        break;
    case SettingsBackAction::ShowProviders:
        settings_.state().close_provider_detail();
        settings_.render_providers();
        break;
    }
    return false;
}

}  // namespace zclaw
