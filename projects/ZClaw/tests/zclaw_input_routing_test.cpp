#include "zclaw_input_model.h"
#include "zclaw_key_event_adapter.h"
#include "zclaw_key_router.h"

#include "keyboard_input.h"

#include <linux/input.h>

#include <cassert>
#include <string>

int main()
{
    using zclaw::InputMode;
    using zclaw::InputSubmissionAction;
    assert(!zclaw::input_is_single_line(InputMode::Chat));
    assert(!zclaw::input_is_single_line(InputMode::SetupEdit));
    assert(!zclaw::input_is_single_line(InputMode::SetupUriEdit));
    assert(!zclaw::input_is_single_line(InputMode::ProviderEdit));
    assert(!zclaw::input_is_single_line(InputMode::ProviderUriEdit));
    assert(zclaw::input_is_single_line(InputMode::PairingCode));
    assert(!zclaw::input_saves_on_close(InputMode::Chat));
    assert(zclaw::input_saves_on_close(InputMode::SetupEdit));
    assert(zclaw::input_saves_on_close(InputMode::SetupUriEdit));
    assert(zclaw::input_saves_on_close(InputMode::ProviderEdit));
    assert(zclaw::input_saves_on_close(InputMode::ProviderUriEdit));
    assert(!zclaw::input_saves_on_close(InputMode::PairingCode));
    zclaw::InputSubmission submission =
        zclaw::input_submission(InputMode::Chat, "hello");
    assert(submission.action == InputSubmissionAction::SendChat);
    assert(submission.value == "hello");
    assert(zclaw::input_submission(InputMode::Chat, "").action ==
           InputSubmissionAction::None);
    assert(zclaw::input_submission(InputMode::PairingCode, "1234").action ==
           InputSubmissionAction::StartPairing);
    assert(zclaw::input_submission(InputMode::PairingCode, "").action ==
           InputSubmissionAction::None);
    assert(zclaw::input_submission(InputMode::SetupEdit, "").action ==
           InputSubmissionAction::ApplySetupEdit);
    assert(zclaw::input_submission(InputMode::SetupUriEdit, "").action ==
           InputSubmissionAction::ApplySetupEdit);
    assert(zclaw::input_submission(InputMode::ProviderEdit, "").action ==
           InputSubmissionAction::ApplyProviderEdit);
    assert(zclaw::input_submission(InputMode::ProviderUriEdit, "").action ==
           InputSubmissionAction::ApplyProviderEdit);

    using zclaw::Key;
    using zclaw::KeyActionType;
    using zclaw::KeyEvent;
    using zclaw::KeyPhase;
    using zclaw::KeyRouteContext;
    using zclaw::StartupState;

    KeyEvent adapted = zclaw::adapt_key_event(
        KEY_ENTER, KBD_KEY_PRESSED, KBD_MOD_SHIFT, "\n");
    assert(adapted.key == Key::Enter);
    assert(adapted.phase == KeyPhase::Pressed);
    assert(adapted.shift && adapted.text == "\n");
    adapted = zclaw::adapt_key_event(
        KEY_BACKSPACE, KBD_KEY_REPEATED, 0, nullptr);
    assert(adapted.key == Key::Backspace);
    assert(adapted.phase == KeyPhase::Repeated);
    assert(!adapted.shift && adapted.text.empty());
    adapted = zclaw::adapt_key_event(KEY_ESC, KBD_KEY_RELEASED, 0, "");
    assert(adapted.key == Key::Escape && adapted.phase == KeyPhase::Released);
    adapted = zclaw::adapt_key_event(KEY_PAGEUP, KBD_KEY_RELEASED, 0, "");
    assert(adapted.key == Key::PageUp && adapted.phase == KeyPhase::Released);
    adapted = zclaw::adapt_key_event(KEY_PAGEDOWN, KBD_KEY_RELEASED, 0, "");
    assert(adapted.key == Key::PageDown && adapted.phase == KeyPhase::Released);
    adapted = zclaw::adapt_key_event(KEY_RESERVED, 99, KBD_MOD_CTRL, "x");
    assert(adapted.key == Key::Other && adapted.phase == KeyPhase::Unknown);
    assert(!adapted.shift && adapted.text == "x");

    const auto routed = [](const KeyRouteContext &context, KeyPhase phase,
                           Key key, bool shift = false,
                           const std::string &text = std::string()) {
        return zclaw::route_key(context, KeyEvent{phase, key, shift, text});
    };

    KeyRouteContext context;
    assert(routed(context, KeyPhase::Released, Key::Enter).type ==
           KeyActionType::None);
    context.startup = StartupState::Offline;
    assert(routed(context, KeyPhase::Pressed, Key::Enter).type ==
           KeyActionType::None);
    assert(routed(context, KeyPhase::Released, Key::Enter).type ==
           KeyActionType::Quit);

    context.startup = StartupState::Ready;
    context.input_open = true;
    zclaw::KeyAction action =
        routed(context, KeyPhase::Pressed, Key::Other, false, "hello");
    assert(action.type == KeyActionType::InputInsertText &&
           action.text == "hello");
    assert(routed(context, KeyPhase::Repeated, Key::Backspace).type ==
           KeyActionType::InputEraseBefore);
    assert(routed(context, KeyPhase::Pressed, Key::Delete).type ==
           KeyActionType::InputEraseAfter);
    assert(routed(context, KeyPhase::Pressed, Key::Left).type ==
           KeyActionType::InputMoveLeft);
    assert(routed(context, KeyPhase::Pressed, Key::Right).type ==
           KeyActionType::InputMoveRight);
    assert(routed(context, KeyPhase::Pressed, Key::Up).type ==
           KeyActionType::InputMoveUp);
    assert(routed(context, KeyPhase::Pressed, Key::Down).type ==
           KeyActionType::InputMoveDown);
    assert(routed(context, KeyPhase::Pressed, Key::Enter, true).type ==
           KeyActionType::InputInsertNewline);
    assert(routed(context, KeyPhase::Pressed, Key::Enter).type ==
           KeyActionType::None);
    assert(routed(context, KeyPhase::Pressed, Key::Other, false, "\n").type ==
           KeyActionType::None);
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::InputClose);
    assert(routed(context, KeyPhase::Released, Key::Enter).type ==
           KeyActionType::InputSubmit);
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::InputInsertNewline);
    context.input_mode = InputMode::SetupEdit;
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::InputSubmit);
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::InputInsertNewline);
    context.input_mode = InputMode::SetupUriEdit;
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::InputSubmit);
    context.input_mode = InputMode::ProviderEdit;
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::InputSubmit);
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::InputInsertNewline);
    context.input_mode = InputMode::PairingCode;
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::InputClose);
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::InputToggleSecretVisibility);
    context.input_mode = InputMode::ProviderUriEdit;
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::InputInsertNewline);
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::InputSubmit);
    assert(routed(context, KeyPhase::Released, Key::Enter, true).type ==
           KeyActionType::None);
    assert(routed(context, KeyPhase::Released, Key::Down).type ==
           KeyActionType::None);

    context.input_open = false;
    context.approval_pending = true;
    assert(routed(context, KeyPhase::Released, Key::Z).type ==
           KeyActionType::ApprovalMoveLeft);
    assert(routed(context, KeyPhase::Released, Key::C).type ==
           KeyActionType::ApprovalMoveRight);
    assert(routed(context, KeyPhase::Released, Key::Enter).type ==
           KeyActionType::ApprovalSubmitSelected);
    assert(routed(context, KeyPhase::Released, Key::Y).type ==
           KeyActionType::ApprovalApprove);
    assert(routed(context, KeyPhase::Released, Key::A).type ==
           KeyActionType::ApprovalAlways);
    assert(routed(context, KeyPhase::Released, Key::N).type ==
           KeyActionType::ApprovalDeny);
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::None);

    context.approval_pending = false;
    context.settings_open = true;
    context.setup_in_flight = true;
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::None);
    context.setup_in_flight = false;
    context.setup_retry_pending = true;
    assert(routed(context, KeyPhase::Released, Key::Enter).type ==
           KeyActionType::SetupRetryActivate);
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::SetupRetryDismiss);
    assert(routed(context, KeyPhase::Released, Key::Backspace).type ==
           KeyActionType::SetupRetryDismiss);
    assert(routed(context, KeyPhase::Released, Key::Left).type ==
           KeyActionType::SetupRetryMoveLeft);
    assert(routed(context, KeyPhase::Released, Key::Right).type ==
           KeyActionType::SetupRetryMoveRight);
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::None);
    assert(routed(context, KeyPhase::Released, Key::Up).type ==
           KeyActionType::None);
    context.setup_retry_pending = false;
    assert(routed(context, KeyPhase::Released, Key::Tab).type ==
           KeyActionType::ToggleSettings);
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::SettingsBack);
    assert(routed(context, KeyPhase::Released, Key::Backspace).type ==
           KeyActionType::SettingsBack);
    assert(routed(context, KeyPhase::Released, Key::Enter).type ==
           KeyActionType::SettingsActivate);
    assert(routed(context, KeyPhase::Released, Key::F).type ==
           KeyActionType::SettingsMoveUp);
    assert(routed(context, KeyPhase::Released, Key::X).type ==
           KeyActionType::SettingsMoveDown);
    assert(routed(context, KeyPhase::Released, Key::Delete).type ==
           KeyActionType::None);
    context.settings_view = zclaw::SettingsView::ProviderDetail;
    assert(routed(context, KeyPhase::Released, Key::Delete).type ==
           KeyActionType::SettingsDeleteProvider);

    context.settings_open = false;
    assert(routed(context, KeyPhase::Unknown, Key::Enter).type ==
           KeyActionType::None);
    assert(routed(context, KeyPhase::Pressed, Key::Enter).type ==
           KeyActionType::None);
    assert(routed(context, KeyPhase::Pressed, Key::Up).type ==
           KeyActionType::None);
    assert(routed(context, KeyPhase::Repeated, Key::Up).type ==
           KeyActionType::ChatScrollUp);
    assert(routed(context, KeyPhase::Repeated, Key::Down).type ==
           KeyActionType::ChatScrollDown);
    assert(routed(context, KeyPhase::Repeated, Key::F).type ==
           KeyActionType::ChatScrollUp);
    assert(routed(context, KeyPhase::Repeated, Key::X).type ==
           KeyActionType::ChatScrollDown);
    assert(routed(context, KeyPhase::Released, Key::Up).type ==
           KeyActionType::ChatScrollUp);
    assert(routed(context, KeyPhase::Released, Key::X).type ==
           KeyActionType::ChatScrollDown);
    assert(routed(context, KeyPhase::Released, Key::PageUp).type ==
           KeyActionType::ChatPageUp);
    assert(routed(context, KeyPhase::Released, Key::PageDown).type ==
           KeyActionType::ChatPageDown);
    assert(routed(context, KeyPhase::Released, Key::Enter).type ==
           KeyActionType::ChatOpenInput);
    assert(routed(context, KeyPhase::Released, Key::Escape).type ==
           KeyActionType::None);
    return 0;
}
