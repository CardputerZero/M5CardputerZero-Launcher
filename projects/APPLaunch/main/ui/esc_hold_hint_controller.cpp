/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_hold_hint_controller.h"

#include "cp0_lvgl_app.h"
#include "cp0_esc_state.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "launcher_toast.h"

namespace {

constexpr uint32_t kPollPeriodMs = 100;

} // namespace

bool EscHoldHintController::handle(const key_item *item)
{
    if (!item || item->key_code != KEY_ESC)
        return false;

    if (item->key_state == KBD_KEY_PRESSED) {
        if (!model_.press(lv_tick_get())) return true;
        if (!poll_timer_)
            poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
        if (poll_timer_) {
            lv_timer_set_period(poll_timer_, kPollPeriodMs);
            lv_timer_reset(poll_timer_);
            lv_timer_resume(poll_timer_);
        } else {
            model_.cancel();
        }
    } else if (item->key_state == KBD_KEY_RELEASED) {
        if (poll_timer_)
            lv_timer_pause(poll_timer_);
        if (model_.release()) launcher_toast().hide();
    }
    return true;
}

void EscHoldHintController::clear_hint_ownership()
{
    model_.clear_hint_ownership();
}

void EscHoldHintController::set_force_home_callback(ForceHomeCallback callback, void *user_data)
{
    force_home_callback_ = callback;
    force_home_user_data_ = user_data;
    if (!callback) {
        if (poll_timer_) lv_timer_pause(poll_timer_);
        if (model_.cancel()) launcher_toast().hide();
    }
}

void EscHoldHintController::shutdown()
{
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    if (model_.cancel()) launcher_toast().hide();
    force_home_callback_ = nullptr;
    force_home_user_data_ = nullptr;
}

void EscHoldHintController::poll(lv_timer_t *timer)
{
    if (!esc_hold_timer_is_current(timer, poll_timer_)) return;
    const EscHoldPollDecision decision = model_.poll(
        lv_tick_get(), cp0_esc_state_read() != 0, force_home_callback_ != nullptr);
    if (decision.hide_hint) launcher_toast().hide();
    if (decision.show_hint)
        launcher_toast().show_persistent("Hold ESC 3s to return home");
    if (decision.force_home) {
        ForceHomeCallback callback = force_home_callback_;
        void *user_data = force_home_user_data_;
        if (callback) callback(user_data);
    }
    if (decision.pause_timer && esc_hold_timer_is_current(timer, poll_timer_))
        lv_timer_pause(timer);
}

void EscHoldHintController::poll_timer_cb(lv_timer_t *timer) noexcept
{
    try {
        auto *controller = static_cast<EscHoldHintController *>(lv_timer_get_user_data(timer));
        if (controller && esc_hold_timer_is_current(timer, controller->poll_timer_))
            controller->poll(timer);
    } catch (...) {
        auto *controller = static_cast<EscHoldHintController *>(lv_timer_get_user_data(timer));
        if (controller && esc_hold_timer_is_current(timer, controller->poll_timer_)) {
            controller->model_.cancel();
            launcher_toast().hide();
            lv_timer_pause(timer);
        }
    }
}

EscHoldHintController &esc_hold_hint_controller()
{
    static EscHoldHintController controller;
    return controller;
}
