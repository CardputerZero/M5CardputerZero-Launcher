#pragma once

#include <string>
#include <utility>
#include <vector>

namespace setting {

template <typename Value>
bool sound_card_commit_if_success(bool success, Value candidate, Value &current)
{
    if (!success) return false;
    current = std::move(candidate);
    return true;
}

template <typename Operation>
bool sound_card_invoke_backend(Operation &&operation) noexcept
{
    try {
        std::forward<Operation>(operation)();
        return true;
    } catch (...) {
        return false;
    }
}

template <typename TimerHandle, typename LabelHandle>
constexpr bool sound_card_cursor_callback_ready(TimerHandle callback_timer,
                                                TimerHandle current_timer,
                                                LabelHandle input_label)
{
    return callback_timer && callback_timer == current_timer && input_label;
}

template <typename TimerHandle, typename LabelHandle>
constexpr bool sound_card_cursor_callback_allowed(TimerHandle callback_timer,
                                                  TimerHandle current_timer,
                                                  LabelHandle input_label,
                                                  bool callback_enabled) noexcept
{
    return callback_enabled &&
           sound_card_cursor_callback_ready(callback_timer, current_timer, input_label);
}

template <typename CallbackTimer, typename CurrentTimer, typename PageHandle>
constexpr bool sound_card_transition_timer_callback_allowed(
    CallbackTimer callback_timer, CurrentTimer current_timer,
    PageHandle page, bool transition_pending) noexcept
{
    return callback_timer && callback_timer == current_timer &&
           page && transition_pending;
}

template <typename ObjectHandle>
constexpr bool sound_card_screen_delete_is_direct(ObjectHandle target,
                                                   ObjectHandle current_target)
{
    return target && target == current_target;
}

template <typename EventTarget, typename CurrentTarget, typename OwnedScreen>
constexpr bool sound_card_transition_screen_delete_matches(
    EventTarget target, CurrentTarget current_target, OwnedScreen owned_screen) noexcept
{
    return target && target == current_target && target == owned_screen;
}

template <typename EventTarget, typename CurrentTarget, typename OwnedLabel>
constexpr bool sound_card_owned_label_delete_matches(EventTarget target,
                                                      CurrentTarget current_target,
                                                      OwnedLabel owned_label) noexcept
{
    return target && target == current_target && target == owned_label;
}

struct SoundCardInfo {
    int index = 0;
    std::string name;
};

struct SoundControlInfo {
    std::string name;
    std::string type;
    int min_value = 0;
    int max_value = 0;
    int step = 1;
    std::string current_text;
    int current_value = 0;
};

class SoundCardModel
{
public:
    static std::vector<SoundCardInfo> parse_cards(const std::string &data);
    static std::vector<SoundControlInfo> parse_controls(const std::string &data);
    static SoundControlInfo parse_detail(const std::string &data,
                                         const SoundControlInfo &fallback);
    static int parse_value(const std::string &text, int fallback);
    static int clamp_value(int value, const SoundControlInfo &control);
};

class SoundCardTransitionModel
{
public:
    bool begin();
    bool complete();
    void cancel() { pending_ = false; }
    bool pending() const { return pending_; }

private:
    bool pending_ = false;
};

} // namespace setting
