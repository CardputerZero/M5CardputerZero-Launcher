#include "zclaw_key_router.h"

namespace zclaw {
namespace {

Key normalize_navigation_key(Key key)
{
    switch (key) {
    case Key::Z:
        return Key::Left;
    case Key::X:
        return Key::Down;
    case Key::C:
        return Key::Right;
    case Key::F:
        return Key::Up;
    default:
        return key;
    }
}

KeyAction input_edit_action(const KeyEvent &event)
{
    switch (event.key) {
    case Key::Enter:
        return {event.shift ? KeyActionType::InputInsertNewline : KeyActionType::None, {}};
    case Key::Backspace:
        return {KeyActionType::InputEraseBefore, {}};
    case Key::Delete:
        return {KeyActionType::InputEraseAfter, {}};
    case Key::Left:
        return {KeyActionType::InputMoveLeft, {}};
    case Key::Right:
        return {KeyActionType::InputMoveRight, {}};
    case Key::Up:
        return {KeyActionType::InputMoveUp, {}};
    case Key::Down:
        return {KeyActionType::InputMoveDown, {}};
    default:
        if (!event.text.empty() && static_cast<unsigned char>(event.text[0]) >= 0x20)
            return {KeyActionType::InputInsertText, event.text};
        return {};
    }
}

}  // namespace

KeyAction route_key(const KeyRouteContext &context, const KeyEvent &event)
{
    if (context.startup != StartupState::Ready) {
        if (context.startup == StartupState::Offline &&
            event.phase == KeyPhase::Released && event.key == Key::Enter)
            return {KeyActionType::Quit, {}};
        return {};
    }

    if (event.phase == KeyPhase::Pressed || event.phase == KeyPhase::Repeated)
        return context.input_open ? input_edit_action(event) : KeyAction{};
    if (event.phase != KeyPhase::Released)
        return {};

    if (context.input_open) {
        if (event.key == Key::Escape)
            return {KeyActionType::InputClose, {}};
        if (event.key == Key::Tab)
            return {KeyActionType::InputToggleSecretVisibility, {}};
        if (event.key == Key::Enter && !event.shift)
            return {KeyActionType::InputSubmit, {}};
        return {};
    }

    const Key key = normalize_navigation_key(event.key);
    if (context.approval_pending) {
        switch (key) {
        case Key::Left:
            return {KeyActionType::ApprovalMoveLeft, {}};
        case Key::Right:
            return {KeyActionType::ApprovalMoveRight, {}};
        case Key::Enter:
            return {KeyActionType::ApprovalSubmitSelected, {}};
        case Key::Y:
            return {KeyActionType::ApprovalApprove, {}};
        case Key::A:
            return {KeyActionType::ApprovalAlways, {}};
        case Key::N:
        case Key::Escape:
        case Key::Backspace:
            return {KeyActionType::ApprovalDeny, {}};
        default:
            return {};
        }
    }

    if (context.setup_retry_pending) {
        if (key == Key::Left)
            return {KeyActionType::SetupRetryMoveLeft, {}};
        if (key == Key::Right)
            return {KeyActionType::SetupRetryMoveRight, {}};
        if (key == Key::Enter)
            return {KeyActionType::SetupRetryActivate, {}};
        if (key == Key::Escape || key == Key::Backspace)
            return {KeyActionType::SetupRetryDismiss, {}};
        return {};
    }

    if (key == Key::Tab && context.setup_in_flight)
        return {};

    if (key == Key::Tab)
        return {KeyActionType::ToggleSettings, {}};

    if (context.settings_open) {
        switch (key) {
        case Key::Escape:
        case Key::Backspace:
            return {KeyActionType::SettingsBack, {}};
        case Key::Enter:
            return {KeyActionType::SettingsActivate, {}};
        case Key::Delete:
            return {context.settings_view == SettingsView::ProviderDetail
                        ? KeyActionType::SettingsDeleteProvider
                        : KeyActionType::None,
                    {}};
        case Key::Up:
            return {KeyActionType::SettingsMoveUp, {}};
        case Key::Down:
            return {KeyActionType::SettingsMoveDown, {}};
        default:
            return {};
        }
    }

    switch (key) {
    case Key::Up:
        return {KeyActionType::ChatScrollUp, {}};
    case Key::Down:
        return {KeyActionType::ChatScrollDown, {}};
    case Key::Enter:
        return {KeyActionType::ChatOpenInput, {}};
    default:
        return {};
    }
}

}  // namespace zclaw
