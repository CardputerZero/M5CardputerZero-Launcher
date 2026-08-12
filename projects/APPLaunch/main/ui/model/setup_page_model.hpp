#pragma once

#include "keyboard_input.h"

#include <functional>
#include <string>
#include <vector>

enum class SetupViewState {
    MAIN, SUB, VALUE_SELECT, HELP, WIFI_LIST, WIFI_SSID, WIFI_PW, WIFI_FORGET_CONFIRM,
    WIFI_POWER_WARNING,
    BT_LIST, BT_ALIAS, BT_POWER_WARNING,
    SOUNDCARD_CARDS, SOUNDCARD_CONTROLS, SOUNDCARD_DETAIL,
    USB_GUIDE, ADB_PAIR, ADB_AUTHORIZATIONS,
};

constexpr cp0_keyboard_input_context_t setup_input_context(SetupViewState view)
{
    return view == SetupViewState::WIFI_SSID || view == SetupViewState::WIFI_PW ||
                   view == SetupViewState::BT_ALIAS
               ? KBD_INPUT_CONTEXT_TEXT
               : KBD_INPUT_CONTEXT_NAVIGATION;
}

class SetupConfirmController
{
public:
    using Action = std::function<void()>;

    void begin(Action action);
    void cancel();
    bool active() const;
    bool resolve(bool confirmed);

private:
    Action action_;
};

class SetupPageLifecycle
{
public:
    bool begin_animation();
    void finish_animation();
    void cancel_animation();
    void root_deleted();

    bool active() const { return active_; }
    bool animating() const { return animating_; }

private:
    bool active_ = true;
    bool animating_ = false;
};

template <typename CurrentTarget, typename TrackedRoot>
constexpr bool setup_root_callback_allowed(
    CurrentTarget current_target, TrackedRoot tracked_root) noexcept
{
    return current_target && current_target == tracked_root;
}

template <typename Action>
bool setup_teardown_step(Action &&action) noexcept
{
    try {
        action();
        return true;
    } catch (...) {
        return false;
    }
}

class SetupPageModel
{
public:
    static constexpr int DEFAULT_MENU_INDEX = 2;
    static constexpr int DEFAULT_CENTER_ROW = 3;

    int selected_index = DEFAULT_MENU_INDEX;
    int sub_selected_index = 0;
    SetupViewState view = SetupViewState::MAIN;
    int value_selected_index = 0;
    std::vector<std::string> value_options;
    std::string value_title;

    bool move_main(int direction, int item_count);
    void enter_sub(int item_count, int center_row = DEFAULT_CENTER_ROW);
    bool move_sub(int direction, int item_count);
    void select_sub(int index, int item_count);
    void enter_value(std::string title, std::vector<std::string> options, int selected_index);
    bool enter_help();
    bool leave_help();
    bool move_value(int direction);
    void leave_to_main();
    void leave_to_sub();

private:
    static int clamp_index(int index, int item_count);
};
