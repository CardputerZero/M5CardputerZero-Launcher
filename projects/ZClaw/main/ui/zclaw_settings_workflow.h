#pragma once

#include "lvgl/lvgl.h"
#include "zclaw_authorization_workflow.h"
#include "zclaw_provider_workflow.h"
#include "zclaw_setup_workflow.h"
#include "zclaw_types.h"

#include <string>

namespace zclaw {

class AsyncService;
class ChatView;
class FontManager;
class InputDialog;
class ProviderManager;
class SettingsCoordinator;
class SoundEffects;
class UiConfigManager;

class SettingsWorkflow {
public:
    SettingsWorkflow(ProviderManager &providers, UiConfigManager &config,
                     SettingsCoordinator &settings, InputDialog &input,
                     FontManager &fonts, ChatView &chat, AsyncService &async_service,
                     SoundEffects &sounds);

    bool setup_in_flight() const;
    bool setup_retry_pending() const;
    void move_setup_retry_selection(int delta);
    bool activate_setup_retry_selection();
    void dismiss_setup_retry();
    void open_setup(lv_obj_t *parent, bool first_run_needed);
    void update_setup_progress(const SetupProgress &progress);

    void apply_setup_edit(const std::string &value);
    void apply_provider_edit(const std::string &value);
    void start_pairing(const std::string &code);
    void delete_current_provider();
    void activate_selection();
    bool navigate_back(bool first_run_needed);

private:
    ProviderManager &providers_;
    UiConfigManager &config_;
    SettingsCoordinator &settings_;
    SoundEffects &sounds_;
    AuthorizationWorkflow authorization_;
    ProviderWorkflow provider_workflow_;
    SetupWorkflow setup_;
};

}  // namespace zclaw
