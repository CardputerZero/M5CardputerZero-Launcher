/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "main.h"
#include "cp0_lvgl_app_runner.hpp"
#include "lvgl/lvgl.h"
#include "hal_lvgl_bsp.h"
#include "cp0_lvgl_file.hpp"
#include "keyboard_input.h"
#include "zclaw_approval_coordinator.h"
#include "zclaw_async_service.h"
#include "zclaw_chat_view.h"
#include "zclaw_chat_workflow.h"
#include "zclaw_input_dialog.h"
#include "zclaw_input_workflow.h"
#include "zclaw_key_event_adapter.h"
#include "zclaw_key_router.h"
#include "zclaw_local_async_backend.h"
#include "zclaw_fonts.hpp"
#include "zclaw_paths.h"
#include "zclaw_provider_manager.h"
#include "zclaw_settings_coordinator.h"
#include "zclaw_settings_workflow.h"
#include "zclaw_sound_effects.h"
#include "zclaw_shell_view.h"
#include "zclaw_startup_view.h"
#include "zclaw_startup_workflow.h"
#include "zclaw_ui_task_queue.h"
#include "zclaw_ui_config_manager.h"
#include "zclaw_ui_action_dispatcher.h"

#include <csignal>
#include <memory>
#include <string>
#include <utility>

namespace {

volatile sig_atomic_t g_quit_requested = 0;

inline const struct key_item *keyboard_item(lv_event_t *event)
{
    return static_cast<const struct key_item *>(lv_event_get_param(event));
}

class ZClawApp : public AppPageRoot
{
    zclaw::ProviderManager provider_manager_{zclaw::paths::providers_config()};
    zclaw::UiConfigManager config_manager_{zclaw::paths::ui_config()};
    std::string avatar_path_;
    std::string storage_warning_;
    std::shared_ptr<zclaw::UiTaskQueue> ui_tasks_ =
        std::make_shared<zclaw::UiTaskQueue>();
    zclaw::AsyncService async_service_{
        ui_tasks_, std::make_shared<zclaw::LocalAsyncBackend>()};
    zclaw::FontManager fonts_;
    zclaw::SoundEffects sounds_;
    zclaw::ShellView shell_view_;
    zclaw::InputDialog input_dialog_;
    zclaw::SettingsCoordinator settings_ui_{provider_manager_, config_manager_,
                                             input_dialog_, fonts_};
    zclaw::ApprovalCoordinator approvals_{fonts_, ui_tasks_};
    zclaw::ChatView chat_view_;
    zclaw::SettingsWorkflow settings_workflow_{
        provider_manager_, config_manager_, settings_ui_, input_dialog_, fonts_,
        chat_view_, async_service_, sounds_};
    zclaw::ChatWorkflow chat_workflow_{
        config_manager_, chat_view_, async_service_, approvals_,
        sounds_,
        [this] { settings_workflow_.open_setup(shell_view_.content(), true); }};
    zclaw::InputWorkflow input_workflow_{
        input_dialog_, settings_workflow_, chat_workflow_};
    zclaw::StartupView startup_view_;
    zclaw::StartupWorkflow startup_workflow_{
        config_manager_, startup_view_, async_service_, ui_tasks_,
        [this] { settings_workflow_.open_setup(shell_view_.content(), true); }};
    zclaw::UiActionDispatcher actions_{
        config_manager_, shell_view_, fonts_, input_dialog_, input_workflow_,
        approvals_, settings_ui_, settings_workflow_, chat_view_,
        [] { g_quit_requested = 1; }};

public:
    ZClawApp()
    {
        fonts_.init();
        avatar_path_ = cp0_file_path("zclaw_avatar_16.png");
        const std::string sparkles_path = cp0_file_path("zclaw_sparkles_10.png");
        const std::string send_button_path = cp0_file_path("zclaw_send_button_18.png");
        load_configuration();
        sounds_.configure(config_manager_.config().ui_sounds_enabled);
        if (!shell_view_.create(root_screen_, &fonts_, avatar_path_, sparkles_path,
                                send_button_path))
            return;
        chat_view_.create(shell_view_.content(), &fonts_, avatar_path_);
        if (!storage_warning_.empty())
            chat_view_.append_assistant_message(storage_warning_);
        event_handler_init();
        startup_workflow_.start(shell_view_.content(), &fonts_);
    }

    ~ZClawApp()
    {
        lv_obj_remove_event_cb_with_user_data(
            root_screen_, ZClawApp::static_lvgl_handler, this);
        approvals_.shutdown();
        async_service_.shutdown();
        ui_tasks_->shutdown();
    }

private:
    void load_configuration()
    {
        std::string error;
        const zclaw::ConfigStoreLoadStatus provider_status =
            provider_manager_.load(&error);
        if (provider_status == zclaw::ConfigStoreLoadStatus::Invalid ||
            provider_status == zclaw::ConfigStoreLoadStatus::Error) {
            storage_warning_ = "Provider settings could not be loaded.";
            if (!error.empty())
                storage_warning_ += "\n" + error;
        }

        error.clear();
        const zclaw::ConfigStoreLoadStatus config_status =
            config_manager_.load(&error);
        if (config_status == zclaw::ConfigStoreLoadStatus::Invalid ||
            config_status == zclaw::ConfigStoreLoadStatus::Error) {
            if (!storage_warning_.empty())
                storage_warning_ += "\n";
            storage_warning_ += "UI settings could not be loaded.";
            if (!error.empty())
                storage_warning_ += "\n" + error;
        }
    }

    void event_handler_init()
    {
        lv_obj_add_event_cb(root_screen_, ZClawApp::static_lvgl_handler, LV_EVENT_ALL, this);
    }

    static void static_lvgl_handler(lv_event_t *e)
    {
        ZClawApp *self = static_cast<ZClawApp *>(lv_event_get_user_data(e));
        if (self)
            self->event_handler(e);
    }

    void event_handler(lv_event_t *e)
    {
        if (lv_event_get_code(e) != static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD))
            return;

        const struct key_item *item = keyboard_item(e);
        if (!item)
            return;

        zclaw::KeyRouteContext context;
        context.startup = startup_workflow_.state();
        context.input_open = input_dialog_.is_open();
        context.input_mode = input_dialog_.mode();
        context.approval_pending = approvals_.pending();
        context.setup_retry_pending =
            settings_workflow_.setup_retry_pending();
        context.setup_in_flight = settings_workflow_.setup_in_flight();
        context.settings_open = settings_ui_.is_open();
        context.settings_view = settings_ui_.state().view();

        const zclaw::KeyEvent event = zclaw::adapt_key_event(
            item->key_code, item->key_state, item->mods, item->utf8);
        actions_.execute(zclaw::route_key(context, event));
    }
};

} // namespace

int run_zclaw_app()
{
    g_quit_requested = 0;
    Cp0LvglAppHooks hooks;
    hooks.should_quit = []() { return g_quit_requested != 0; };
    return cp0_lvgl_run_app<ZClawApp>(std::move(hooks));
}
