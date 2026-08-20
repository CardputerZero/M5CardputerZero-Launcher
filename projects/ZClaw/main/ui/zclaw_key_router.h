#pragma once

#include "zclaw_input_model.h"
#include "zclaw_settings_navigation_model.h"
#include "zclaw_startup_model.h"

#include <string>

namespace zclaw {

enum class KeyPhase {
    Unknown,
    Pressed,
    Repeated,
    Released,
};

enum class Key {
    Other,
    Enter,
    Escape,
    Backspace,
    Delete,
    Tab,
    Left,
    Right,
    Up,
    Down,
    A,
    C,
    F,
    N,
    X,
    Y,
    Z,
};

struct KeyEvent {
    KeyPhase phase = KeyPhase::Unknown;
    Key key = Key::Other;
    bool shift = false;
    std::string text;
    bool fn = false;
};

struct KeyRouteContext {
    StartupState startup = StartupState::CheckingNetwork;
    bool input_open = false;
    InputMode input_mode = InputMode::Chat;
    bool approval_pending = false;
    bool setup_retry_pending = false;
    bool setup_in_flight = false;
    bool settings_open = false;
    SettingsView settings_view = SettingsView::Main;
};

enum class KeyActionType {
    None,
    Quit,
    InputInsertText,
    InputInsertNewline,
    InputEraseBefore,
    InputEraseAfter,
    InputMoveLeft,
    InputMoveRight,
    InputMoveUp,
    InputMoveDown,
    InputToggleSecretVisibility,
    InputClose,
    InputSubmit,
    ApprovalMoveLeft,
    ApprovalMoveRight,
    ApprovalSubmitSelected,
    ApprovalApprove,
    ApprovalAlways,
    ApprovalDeny,
    SetupRetryMoveLeft,
    SetupRetryMoveRight,
    SetupRetryActivate,
    SetupRetryDismiss,
    ToggleSettings,
    SettingsBack,
    SettingsActivate,
    SettingsDeleteProvider,
    SettingsMoveUp,
    SettingsMoveDown,
    ChatScrollUp,
    ChatScrollDown,
    ChatOpenInput,
};

struct KeyAction {
    KeyActionType type = KeyActionType::None;
    std::string text;
};

KeyAction route_key(const KeyRouteContext &context, const KeyEvent &event);

}  // namespace zclaw
