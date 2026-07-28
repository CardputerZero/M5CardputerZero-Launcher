#include "zclaw_ui_action_dispatcher.h"

#include "zclaw_approval_coordinator.h"
#include "zclaw_chat_view.h"
#include "zclaw_fonts.hpp"
#include "zclaw_input_dialog.h"
#include "zclaw_input_workflow.h"
#include "zclaw_runtime_state.h"
#include "zclaw_settings_coordinator.h"
#include "zclaw_settings_workflow.h"
#include "zclaw_shell_view.h"
#include "zclaw_ui_config_manager.h"

#include <utility>

namespace zclaw {

UiActionDispatcher::UiActionDispatcher(
    UiConfigManager &config, ShellView &shell, FontManager &fonts,
    InputDialog &input, InputWorkflow &input_workflow,
    ApprovalCoordinator &approvals, SettingsCoordinator &settings,
    SettingsWorkflow &settings_workflow, ChatView &chat,
    RequestQuit request_quit)
    : config_(config), shell_(shell), fonts_(fonts), input_(input),
      input_workflow_(input_workflow), approvals_(approvals), settings_(settings),
      settings_workflow_(settings_workflow), chat_(chat),
      request_quit_(std::move(request_quit))
{
}

void UiActionDispatcher::execute(const KeyAction &action)
{
    switch (action.type) {
    case KeyActionType::None:
        break;
    case KeyActionType::Quit:
        request_quit();
        break;
    case KeyActionType::InputInsertText:
        input_.append(action.text.c_str());
        break;
    case KeyActionType::InputInsertNewline:
        input_.insert_newline();
        break;
    case KeyActionType::InputEraseBefore:
        input_.erase_before_cursor();
        break;
    case KeyActionType::InputEraseAfter:
        input_.erase_after_cursor();
        break;
    case KeyActionType::InputMoveLeft:
        input_.move_left();
        break;
    case KeyActionType::InputMoveRight:
        input_.move_right();
        break;
    case KeyActionType::InputMoveUp:
        input_.move_up();
        break;
    case KeyActionType::InputMoveDown:
        input_.move_down();
        break;
    case KeyActionType::InputToggleSecretVisibility:
        input_.toggle_secret_visibility();
        break;
    case KeyActionType::InputClose:
        input_.close();
        break;
    case KeyActionType::InputSubmit:
        input_workflow_.submit_current();
        break;
    case KeyActionType::ApprovalMoveLeft:
        approvals_.move_selection(-1);
        break;
    case KeyActionType::ApprovalMoveRight:
        approvals_.move_selection(1);
        break;
    case KeyActionType::ApprovalSubmitSelected:
        approvals_.answer_selected();
        break;
    case KeyActionType::ApprovalApprove:
        approvals_.answer("approve");
        break;
    case KeyActionType::ApprovalAlways:
        approvals_.answer("always");
        break;
    case KeyActionType::ApprovalDeny:
        approvals_.answer("deny");
        break;
    case KeyActionType::SetupRetryMoveLeft:
        settings_workflow_.move_setup_retry_selection(-1);
        break;
    case KeyActionType::SetupRetryMoveRight:
        settings_workflow_.move_setup_retry_selection(1);
        break;
    case KeyActionType::SetupRetryActivate:
        if (settings_workflow_.activate_setup_retry_selection())
            request_quit();
        break;
    case KeyActionType::SetupRetryDismiss:
        settings_workflow_.dismiss_setup_retry();
        break;
    case KeyActionType::ToggleSettings:
        if (settings_.is_open())
            settings_.close();
        else
            settings_.open(shell_.content());
        break;
    case KeyActionType::SettingsBack:
        if (settings_workflow_.navigate_back(
                RuntimeState::first_run_needed(config_.config())))
            request_quit();
        break;
    case KeyActionType::SettingsActivate:
        settings_workflow_.activate_selection();
        break;
    case KeyActionType::SettingsDeleteProvider:
        settings_workflow_.delete_current_provider();
        break;
    case KeyActionType::SettingsMoveUp:
        settings_.move_selection(-1);
        break;
    case KeyActionType::SettingsMoveDown:
        settings_.move_selection(1);
        break;
    case KeyActionType::ChatScrollUp:
        chat_.scroll(24);
        break;
    case KeyActionType::ChatScrollDown:
        chat_.scroll(-24);
        break;
    case KeyActionType::ChatOpenInput:
        input_.open_chat(&fonts_);
        break;
    }
}

void UiActionDispatcher::request_quit()
{
    if (request_quit_)
        request_quit_();
}

}  // namespace zclaw
