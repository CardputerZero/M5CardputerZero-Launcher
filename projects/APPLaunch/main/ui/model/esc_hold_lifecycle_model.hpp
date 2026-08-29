#pragma once

#include <cstdint>

#include "cp0_esc_exit_policy.hpp"

struct EscHoldPollDecision
{
    bool show_hint = false;
    bool hide_hint = false;
    bool force_home = false;
    bool pause_timer = false;
};

class EscHoldLifecycleModel
{
public:
    static constexpr uint32_t HINT_DELAY_MS = cp0_esc_exit_policy::HINT_DELAY_MS;
    static constexpr uint32_t FORCE_HOME_DELAY_MS = cp0_esc_exit_policy::TERMINATE_DELAY_MS;

    bool press(uint32_t now);
    bool release();
    bool cancel();
    void clear_hint_ownership();
    EscHoldPollDecision poll(uint32_t now, bool hardware_key_down,
                             bool force_callback_available,
                             bool return_home_enabled = true);

    bool holding() const { return holding_; }

private:
    void reset();

    uint32_t down_tick_ = 0;
    bool holding_ = false;
    bool hint_shown_ = false;
    bool hint_owned_ = false;
    bool force_home_sent_ = false;
};

template <typename CallbackTimer, typename ActiveTimer>
constexpr bool esc_hold_timer_is_current(CallbackTimer callback_timer,
                                         ActiveTimer active_timer)
{
    return callback_timer && callback_timer == active_timer;
}
