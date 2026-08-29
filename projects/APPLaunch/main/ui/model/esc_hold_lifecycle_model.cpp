#include "esc_hold_lifecycle_model.hpp"

bool EscHoldLifecycleModel::press(uint32_t now)
{
    if (holding_) return false;
    holding_ = true;
    down_tick_ = now;
    hint_shown_ = false;
    hint_owned_ = false;
    force_home_sent_ = false;
    return true;
}

bool EscHoldLifecycleModel::release()
{
    const bool hide_hint = hint_owned_;
    reset();
    return hide_hint;
}

bool EscHoldLifecycleModel::cancel()
{
    return release();
}

void EscHoldLifecycleModel::clear_hint_ownership()
{
    hint_owned_ = false;
}

EscHoldPollDecision EscHoldLifecycleModel::poll(uint32_t now, bool hardware_key_down,
                                               bool force_callback_available,
                                               bool return_home_enabled)
{
    EscHoldPollDecision decision;
    if (!holding_) {
        decision.pause_timer = true;
        return decision;
    }
    if (!return_home_enabled) {
        decision.hide_hint = release();
        decision.pause_timer = true;
        return decision;
    }
    if (!hardware_key_down) {
        decision.hide_hint = release();
        decision.pause_timer = true;
        return decision;
    }

    const uint32_t elapsed = now - down_tick_;
    if (elapsed >= HINT_DELAY_MS && !hint_shown_) {
        hint_shown_ = true;
        hint_owned_ = true;
        decision.show_hint = true;
    }
    if (elapsed >= FORCE_HOME_DELAY_MS && !force_home_sent_ &&
        force_callback_available) {
        force_home_sent_ = true;
        decision.force_home = true;
        decision.pause_timer = true;
    }
    return decision;
}

void EscHoldLifecycleModel::reset()
{
    down_tick_ = 0;
    holding_ = false;
    hint_shown_ = false;
    hint_owned_ = false;
    force_home_sent_ = false;
}
