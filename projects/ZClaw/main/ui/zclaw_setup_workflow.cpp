#include "zclaw_setup_workflow.h"

#include "zclaw_async_service.h"
#include "zclaw_chat_view.h"
#include "zclaw_fonts.hpp"
#include "zclaw_input_dialog.h"
#include "zclaw_provider_catalog.h"
#include "zclaw_provider_manager.h"
#include "zclaw_settings_coordinator.h"
#include "zclaw_secret_input_model.h"
#include "zclaw_ui_config_manager.h"

#include <cstddef>
#include <utility>

namespace zclaw {

SetupWorkflow::SetupWorkflow(ProviderManager &providers, UiConfigManager &config,
                             SettingsCoordinator &settings, InputDialog &input,
                             FontManager &fonts, ChatView &chat,
                             AsyncService &async_service)
    : providers_(providers), config_(config), settings_(settings), input_(input),
      fonts_(fonts), chat_(chat), async_service_(async_service),
      lifetime_(std::make_shared<CallbackLifetime<SetupWorkflow>>(this))
{
}

SetupWorkflow::~SetupWorkflow()
{
    lifetime_->invalidate();
}

bool SetupWorkflow::in_flight() const
{
    return in_flight_;
}

bool SetupWorkflow::retry_pending() const
{
    return retry_pending_;
}

void SetupWorkflow::open(lv_obj_t *parent, bool first_run_needed)
{
    settings_.open_setup(parent, first_run_needed, in_flight_);
}

void SetupWorkflow::render()
{
    settings_.render_setup(in_flight_);
}

void SetupWorkflow::update_progress(const SetupProgress &progress)
{
    settings_.update_setup_progress(progress, in_flight_);
}

void SetupWorkflow::select_provider(int preset_index)
{
    if (preset_index < 0 ||
        static_cast<std::size_t>(preset_index) >= provider_preset_count())
        return;
    const ProviderConfig selected =
        provider_preset(static_cast<std::size_t>(preset_index));
    std::string error;
    if (!providers_.activate(selected, &error))
        report_save_error(error);
    settings_.state().set_selected_row(0);
    render();
}

void SetupWorkflow::edit_selected_field()
{
    settings_.state().set_setup_edit_field(setup_edit_field_for_row(
        providers_.setup_provider(), settings_.state().selected_row()));
    ProviderConfig provider = providers_.setup_provider();
    if (settings_.state().setup_edit_field() == SetupEditField::WireApi) {
        toggle_wire_api(&provider);
        settings_.state().set_setup_edit_field(SetupEditField::None);
        std::string error;
        if (!providers_.update_setup_provider(provider, &error))
            report_save_error(error);
        render();
        return;
    }
    const std::string *value =
        setup_field_value(&provider, settings_.state().setup_edit_field());
    if (value) {
        const bool secret =
            settings_.state().setup_edit_field() == SetupEditField::ApiKey;
        input_.open_text(&fonts_,
                         setup_field_name(settings_.state().setup_edit_field()),
                         secret ? secret_input_initial_text() : *value,
                         InputMode::SetupEdit, secret);
    }
}

void SetupWorkflow::apply_edit(const std::string &value)
{
    ProviderConfig provider = providers_.setup_provider();
    std::string *field =
        setup_field_value(&provider, settings_.state().setup_edit_field());
    if (field) {
        *field = settings_.state().setup_edit_field() == SetupEditField::ApiKey
                     ? apply_secret_input(*field, value)
                     : value;
    }
    settings_.state().set_setup_edit_field(SetupEditField::None);
    std::string error;
    if (!providers_.update_setup_provider(provider, &error))
        report_save_error(error);
    render();
}

void SetupWorkflow::start()
{
    if (in_flight_)
        return;
    std::string validation_error;
    if (!settings_.validate_setup(&validation_error)) {
        chat_.append_assistant_message(validation_error);
        render();
        return;
    }

    retry_pending_ = false;
    settings_.close_setup_error();
    in_flight_ = true;
    chat_.append_assistant_message("Configuring ZeroClaw service...");
    render();
    const std::shared_ptr<CallbackLifetime<SetupWorkflow>> lifetime = lifetime_;
    if (!async_service_.setup(
            config_.config(), providers_.setup_provider(),
            [lifetime](SetupProgress progress) {
                lifetime->invoke([&progress](SetupWorkflow &workflow) {
                    workflow.update_progress(progress);
                });
            },
            [lifetime](OperationResult result) {
                lifetime->invoke([&result](SetupWorkflow &workflow) {
                    workflow.finish(std::move(result));
                });
            })) {
        in_flight_ = false;
        chat_.append_assistant_message("Could not start setup thread.");
        render();
    }
}

void SetupWorkflow::finish(OperationResult result)
{
    in_flight_ = false;
    bool persisted = result.ok;
    chat_.append_assistant_message(result.text);
    if (!result.ok && settings_.is_open() &&
        settings_.state().view() == SettingsView::Setup) {
        retry_pending_ = true;
        retry_selection_ = 0;
        settings_.show_setup_error(result.text);
        return;
    }
    if (result.ok) {
        std::string config_error;
        if (!config_.update(result.config, &config_error)) {
            report_save_error(config_error);
            persisted = false;
        }
    }
    if (persisted && settings_.is_open() &&
        settings_.state().view() == SettingsView::Setup) {
        render();
        settings_.close();
    }
}

void SetupWorkflow::move_retry_selection(int delta)
{
    if (!retry_pending_ || delta == 0)
        return;
    retry_selection_ = delta < 0 ? 0 : 1;
    settings_.update_setup_error_selection(retry_selection_);
}

bool SetupWorkflow::activate_retry_selection()
{
    if (!retry_pending_)
        return false;
    if (retry_selection_ == 0) {
        start();
        return false;
    }
    retry_pending_ = false;
    settings_.close_setup_error();
    return true;
}

void SetupWorkflow::dismiss_error()
{
    if (!retry_pending_)
        return;
    retry_pending_ = false;
    settings_.close_setup_error();
}

void SetupWorkflow::report_save_error(const std::string &error)
{
    chat_.append_assistant_message(
        error.empty() ? "Could not save settings." : error);
}

}  // namespace zclaw
