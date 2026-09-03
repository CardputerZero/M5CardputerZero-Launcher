#pragma once

#include <cstddef>

constexpr bool lora_info_response_valid(int code, std::size_t payload_size,
                                        std::size_t expected_size)
{
    return code == 0 && payload_size == expected_size;
}

constexpr bool lora_poll_callback_allowed(bool timer_is_current,
                                          bool app_active) noexcept
{
    return timer_is_current && app_active;
}

template <typename Target>
constexpr bool lora_animation_callback_allowed(Target target) noexcept
{
    return static_cast<bool>(target);
}

template <typename Target, typename CurrentTarget>
constexpr bool lora_owned_delete_callback_allowed(
    Target target, CurrentTarget current_target) noexcept
{
    return target && target == current_target;
}

template <typename CurrentTarget, typename OwnedControl>
constexpr bool lora_send_action_callback_allowed(
    CurrentTarget current_target, OwnedControl owned_control,
    bool app_active, bool send_view_active) noexcept
{
    return current_target && current_target == owned_control &&
           app_active && send_view_active;
}

template <typename CurrentTarget, typename PageRoot>
constexpr bool lora_page_event_callback_allowed(
    CurrentTarget current_target, PageRoot page_root, bool app_active) noexcept
{
    return current_target && current_target == page_root && app_active;
}

template <typename Handle>
bool lora_page_ui_ready(Handle page_root, Handle messages_view, Handle message_list,
                        Handle info_view, Handle send_view, Handle page_indicator)
{
    return page_root && messages_view && message_list && info_view && send_view && page_indicator;
}

template <typename... Handles>
bool lora_page_controls_ready(Handles... handles)
{
    return (... && static_cast<bool>(handles));
}
