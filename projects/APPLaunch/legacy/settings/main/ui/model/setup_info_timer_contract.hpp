#pragma once

template <typename Timer, typename Page>
constexpr bool setup_info_timer_callback_ready(
    Timer callback_timer, bool timer_is_current, Page page)
{
    return callback_timer && timer_is_current && page;
}

template <typename Target, typename CurrentTarget>
constexpr bool setup_info_label_delete_callback_allowed(
    Target target, CurrentTarget current_target) noexcept
{
    return target && target == current_target;
}
