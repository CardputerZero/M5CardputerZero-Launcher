#include "zclaw_provider_workflow.h"

#include "zclaw_chat_view.h"
#include "zclaw_fonts.hpp"
#include "zclaw_input_dialog.h"
#include "zclaw_provider_catalog.h"
#include "zclaw_provider_manager.h"
#include "zclaw_settings_coordinator.h"
#include "zclaw_settings_panel.h"

#include <cstddef>

namespace zclaw {

ProviderWorkflow::ProviderWorkflow(ProviderManager &providers,
                                   SettingsCoordinator &settings,
                                   InputDialog &input, FontManager &fonts,
                                   ChatView &chat)
    : providers_(providers), settings_(settings), input_(input), fonts_(fonts),
      chat_(chat)
{
}

void ProviderWorkflow::add()
{
    const int next = static_cast<int>(providers_.providers().size()) + 1;
    ProviderConfig provider;
    provider.alias = "provider" + std::to_string(next);
    provider.family = "openai-compatible";
    provider.model = "model";
    provider.uri = "https://api.example.com/v1";
    std::string error;
    std::size_t provider_index = 0;
    if (!providers_.add(provider, &provider_index, &error)) {
        report_save_error(error);
        return;
    }
    settings_.state().set_provider_selected(
        static_cast<int>(provider_index) + 1,
        static_cast<int>(providers_.providers().size()) + 1,
        SettingsPanel::kMaximumRows);
    open_detail(static_cast<int>(provider_index));
}

void ProviderWorkflow::open_detail(int provider_index)
{
    settings_.state().open_provider_detail(provider_index);
    settings_.render_provider_detail();
}

void ProviderWorkflow::edit_selected_field()
{
    const int provider_index = settings_.state().provider_detail_index();
    if (provider_index < 0 ||
        provider_index >= static_cast<int>(providers_.providers().size()))
        return;
    settings_.state().set_provider_edit_field(
        provider_edit_field_for_row(settings_.state().selected_row()));
    if (settings_.state().provider_edit_field() == ProviderEditField::None)
        return;
    ProviderConfig provider = providers_.providers()[provider_index];
    if (settings_.state().provider_edit_field() == ProviderEditField::WireApi) {
        toggle_wire_api(&provider);
        settings_.state().set_provider_edit_field(ProviderEditField::None);
        std::string error;
        if (!providers_.replace(static_cast<std::size_t>(provider_index), provider,
                                &error))
            report_save_error(error);
        settings_.render_provider_detail();
        return;
    }
    const std::string *value = provider_field_value(
        &provider, settings_.state().provider_edit_field());
    if (value)
        input_.open_text(&fonts_,
                         provider_field_name(settings_.state().provider_edit_field()),
                         *value, InputMode::ProviderEdit);
}

void ProviderWorkflow::apply_edit(const std::string &value)
{
    const int provider_index = settings_.state().provider_detail_index();
    if (provider_index < 0 ||
        provider_index >= static_cast<int>(providers_.providers().size()) ||
        settings_.state().provider_edit_field() == ProviderEditField::None)
        return;

    ProviderConfig provider = providers_.providers()[provider_index];
    std::string *field = provider_field_value(
        &provider, settings_.state().provider_edit_field());
    if (field)
        *field = value;
    settings_.state().set_provider_edit_field(ProviderEditField::None);
    std::string error;
    if (!providers_.replace(static_cast<std::size_t>(provider_index), provider,
                            &error))
        report_save_error(error);
    settings_.render_provider_detail();
}

void ProviderWorkflow::delete_current()
{
    const int provider_index = settings_.state().provider_detail_index();
    if (provider_index < 0 ||
        provider_index >= static_cast<int>(providers_.providers().size()))
        return;
    std::string error;
    if (!providers_.erase(static_cast<std::size_t>(provider_index),
                          provider_preset(0), &error)) {
        report_save_error(error);
        return;
    }
    settings_.state().close_provider_detail();
    settings_.render_providers();
}

void ProviderWorkflow::report_save_error(const std::string &error)
{
    chat_.append_assistant_message(
        error.empty() ? "Could not save settings." : error);
}

}  // namespace zclaw
