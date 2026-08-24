#pragma once

template <typename CallbackTimer, typename ActiveTimer, typename PageHandle>
constexpr bool bluetooth_scan_callback_ready(CallbackTimer callback_timer,
                                             ActiveTimer active_timer,
                                             PageHandle page)
{
    return callback_timer && callback_timer == active_timer && page;
}

template <typename CallbackTimer, typename ActiveTimer, typename PageHandle>
constexpr bool bluetooth_scan_callback_allowed(CallbackTimer callback_timer,
                                                ActiveTimer active_timer,
                                                PageHandle page,
                                                bool callback_enabled) noexcept
{
    return callback_enabled &&
           bluetooth_scan_callback_ready(callback_timer, active_timer, page);
}

template <typename CallbackTimer, typename ActiveTimer, typename PageHandle>
constexpr bool bluetooth_feedback_callback_ready(CallbackTimer callback_timer,
                                                  ActiveTimer active_timer,
                                                  PageHandle page)
{
    return callback_timer && callback_timer == active_timer && page;
}

template <typename CallbackTimer, typename ActiveTimer, typename PageHandle>
constexpr bool bluetooth_feedback_callback_allowed(CallbackTimer callback_timer,
                                                    ActiveTimer active_timer,
                                                    PageHandle page,
                                                    bool feedback_pending) noexcept
{
    return feedback_pending &&
           bluetooth_feedback_callback_ready(callback_timer, active_timer, page);
}

template <typename ObjectHandle>
constexpr bool bluetooth_screen_delete_is_direct(ObjectHandle target,
                                                  ObjectHandle current_target)
{
    return target && target == current_target;
}

template <typename EventTarget, typename CurrentTarget, typename TrackedScreen>
constexpr bool bluetooth_feedback_screen_delete_matches(
    EventTarget target, CurrentTarget current_target,
    TrackedScreen tracked_screen) noexcept
{
    return target && target == current_target && target == tracked_screen;
}

constexpr bool bluetooth_scan_action_needs_watchdog_recovery(bool action_busy,
                                                              bool operation_active)
{
    return action_busy && !operation_active;
}

constexpr bool bluetooth_action_entry_allowed(bool action_busy,
                                               bool operation_active) noexcept
{
    return !action_busy || !operation_active;
}

template <typename PageHandle>
constexpr bool bluetooth_async_completion_allowed(bool token_current,
                                                  PageHandle page) noexcept
{
    return token_current && page;
}

constexpr bool bluetooth_scan_action_should_resume(bool from_scan, int result_code)
{
    return from_scan && result_code != 0;
}
