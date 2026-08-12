/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "launcher_platform.hpp"
#include "lvgl/src/widgets/gif/lv_gif.h"
#include "sample_log.h"

#include <cstdio>
#include <new>
#include <string>

namespace {

constexpr int kStartupSoundRetryMax = 10;
constexpr uint32_t kStartupSoundRetryMs = 500;
constexpr uint32_t kStartupLogoDelayMs = 1000;

UILaunchPage *page_from_event(lv_event_t *event)
{
    return event ? static_cast<UILaunchPage *>(lv_event_get_user_data(event)) : nullptr;
}

} // namespace

void UILaunchPage::show_home_screen()
{
    stop_startup_presentation();
    if (!screen()) return;
    SLOGI("[HOME] loading launcher home screen");
    use_bold_home_title_font();
    lv_disp_load_scr(screen());
    bind_home_input_group();
}

void UILaunchPage::load_home_screen()
{
    if (!startup_delay_model_.begin())
        return;
    play_startup_sound_with_retry();

    const std::string logo_path = launcher_platform::path("logo_lcd.png");
    std::snprintf(startup_logo_path_.data(), startup_logo_path_.size(), "%s", logo_path.c_str());
    startup_logo_scr_ = lv_obj_create(nullptr);
    if (!startup_logo_scr_) {
        stop_startup_delay();
        show_home_screen();
        return;
    }
    lv_obj_add_event_cb(startup_logo_scr_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    lv_obj_set_style_bg_color(startup_logo_scr_, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(startup_logo_scr_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *logo = lv_img_create(startup_logo_scr_);
    if (!logo) {
        release_startup_screen(startup_logo_scr_);
        stop_startup_delay();
        show_home_screen();
        return;
    }
    lv_img_set_src(logo, startup_logo_path_.data());
    lv_obj_center(logo);
    lv_disp_load_scr(startup_logo_scr_);

    if (!startup_delay_timer_) {
        auto *context = new (std::nothrow) StartupTimerContext{
            this, startup_delay_generation_.begin()};
        if (context)
            startup_delay_timer_ =
                lv_timer_create(startup_delay_timer_cb, kStartupLogoDelayMs, context);
        if (startup_delay_timer_) {
            startup_delay_context_ = context;
            lv_timer_set_repeat_count(startup_delay_timer_, 1);
            startup_delay_model_.timer_created(true);
        } else if (startup_delay_model_.timer_created(false)) {
            if (context) {
                startup_delay_generation_.stop(context->generation);
                delete context;
            }
            finish_startup_delay();
        }
    }
}

void UILaunchPage::finish_startup_delay()
{
    show_home_screen();
}

void UILaunchPage::stop_startup_presentation()
{
    // Returning from an app must not leave a startup callback capable of
    // replacing the home screen with the boot logo or GIF.
    startup_gif_done_ = true;
    stop_startup_delay();
    // Audio readiness is independent of the splash lifetime. Keep its bounded
    // retry alive after home appears; the destructor still owns and stops it.
    release_startup_screen(startup_logo_scr_);
    release_startup_screen(startup_gif_);
}

void UILaunchPage::play_startup_sound_with_retry()
{
    int play_result = -1;
    try {
        cp0_signal_audio_api({"SystemSoundPlay", "0"}, [&](int code, std::string) {
            play_result = code;
        });
    } catch (...) {
        play_result = -1;
    }

    if (play_result == 0) {
        stop_startup_sound_timer();
        startup_sound_retry_count_ = 0;
        return;
    }
    if (startup_sound_timer_)
        return;

    startup_sound_retry_count_ = 0;
    auto *context = new (std::nothrow) StartupTimerContext{
        this, startup_sound_generation_.begin()};
    if (!context) return;
    startup_sound_timer_ = lv_timer_create(startup_sound_timer_cb, kStartupSoundRetryMs, context);
    if (startup_sound_timer_)
        startup_sound_context_ = context;
    else {
        startup_sound_generation_.stop(context->generation);
        delete context;
    }
}

void UILaunchPage::stop_startup_sound_timer()
{
    if (!startup_sound_timer_)
        return;
    lv_timer_delete(startup_sound_timer_);
    startup_sound_timer_ = nullptr;
    if (startup_sound_context_) {
        startup_sound_generation_.stop(startup_sound_context_->generation);
        delete startup_sound_context_;
        startup_sound_context_ = nullptr;
    }
}

void UILaunchPage::stop_startup_delay()
{
    startup_delay_model_.stop();
    if (startup_delay_timer_) {
        lv_timer_delete(startup_delay_timer_);
        startup_delay_timer_ = nullptr;
    }
    if (startup_delay_context_) {
        startup_delay_generation_.stop(startup_delay_context_->generation);
        delete startup_delay_context_;
        startup_delay_context_ = nullptr;
    }
}

void UILaunchPage::start_startup_gif()
{
    if (startup_gif_) return;
    const std::string gif_path = launcher_platform::path("logo_output.gif");
    std::snprintf(startup_gif_path_.data(), startup_gif_path_.size(), "%s", gif_path.c_str());
    startup_gif_done_ = false;
    startup_gif_ = lv_gif_create(nullptr);
    if (!startup_gif_) {
        load_home_screen();
        return;
    }
    lv_obj_add_event_cb(startup_gif_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    lv_gif_set_src(startup_gif_, startup_gif_path_.data());
    lv_obj_center(startup_gif_);
    lv_obj_add_event_cb(startup_gif_, on_startup_gif_event, LV_EVENT_ALL, this);
    lv_disp_load_scr(startup_gif_);
}

void UILaunchPage::handle_startup_gif_event(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_READY || startup_gif_done_ ||
        lv_event_get_target(event) != startup_gif_ ||
        lv_event_get_current_target(event) != startup_gif_)
        return;

    startup_gif_done_ = true;
    if (startup_gif_)
        lv_gif_pause(startup_gif_);
    load_home_screen();
}

void UILaunchPage::on_startup_gif_event(lv_event_t *event) noexcept
{
    try {
        if (UILaunchPage *self = page_from_event(event))
            self->handle_startup_gif_event(event);
    } catch (...) {
    }
}

void UILaunchPage::startup_sound_timer_cb(lv_timer_t *timer) noexcept
{
    UILaunchPage *self = nullptr;
    try {
        auto *context = static_cast<StartupTimerContext *>(lv_timer_get_user_data(timer));
        self = context ? context->page : nullptr;
        if (!self || context != self->startup_sound_context_ ||
            !self->startup_sound_generation_.current(context->generation) ||
            !launcher_startup_timer_is_current(timer, self->startup_sound_timer_))
            return;

        ++self->startup_sound_retry_count_;
        if (self->startup_sound_retry_count_ > kStartupSoundRetryMax) {
            self->stop_startup_sound_timer();
            return;
        }
        self->play_startup_sound_with_retry();
    } catch (...) {
        if (self) self->stop_startup_sound_timer();
    }
}

void UILaunchPage::startup_delay_timer_cb(lv_timer_t *timer) noexcept
{
    try {
        auto *context = static_cast<StartupTimerContext *>(lv_timer_get_user_data(timer));
        UILaunchPage *self = context ? context->page : nullptr;
        if (!self) return;

        const bool context_is_owned = context == self->startup_delay_context_;
        const auto decision = launcher_startup_one_shot_decision(
            launcher_startup_timer_is_current(timer, self->startup_delay_timer_),
            context_is_owned,
            context_is_owned && self->startup_delay_generation_.current(context->generation));
        if (!decision.retire_timer) return;

        self->startup_delay_timer_ = nullptr;
        if (decision.release_context) {
            self->startup_delay_context_ = nullptr;
            self->startup_delay_generation_.stop(context->generation);
            delete context;
        }
        if (decision.complete && self->startup_delay_model_.complete())
            self->finish_startup_delay();
    } catch (...) {
    }
}
