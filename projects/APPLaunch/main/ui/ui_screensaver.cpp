/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_screensaver.h"

#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"
#include "model/screensaver_model.hpp"
#include "model/screensaver_runtime_contract.hpp"

#include <cstdlib>
#include <utility>

namespace {

constexpr uint32_t kIdleCheckMs = 500;
constexpr uint32_t kAnimationFrameMs = 40;

#define SCREEN_ICON_PIXELS \
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00, 0x0f,0xff,0xff,0xff,0xf0, \
    0x0f,0xff,0xff,0xff,0xf0, 0x30,0x00,0x00,0x00,0x0c, \
    0x30,0x00,0x00,0x00,0x0c, 0x30,0x00,0x00,0x00,0x0c, \
    0x31,0xff,0xff,0xff,0x8c, 0x31,0xff,0xff,0xff,0x8c, \
    0x36,0x00,0x00,0x00,0x6c, 0x36,0x00,0x00,0x00,0x6c, \
    0x36,0x00,0x00,0x00,0x6c, 0x36,0x00,0x00,0x00,0x6c, \
    0x36,0x00,0x00,0x00,0x6c, 0x36,0x00,0x00,0x00,0x6c, \
    0x36,0x00,0x00,0x00,0x6c, 0x36,0x00,0x00,0x00,0x6c, \
    0x36,0x00,0x00,0x00,0x6c, 0x36,0x00,0x00,0x00,0x6c, \
    0x36,0x00,0x00,0x00,0x6c, 0x36,0x00,0x00,0x00,0x6c, \
    0x36,0x00,0x00,0x00,0x6c, 0x36,0x00,0x00,0x00,0x6c, \
    0x36,0x00,0x00,0x00,0x6c, 0x36,0x00,0x00,0x00,0x6c, \
    0x36,0x00,0x00,0x00,0x6c, 0x31,0xff,0xff,0xff,0x8c, \
    0x31,0xff,0xff,0xff,0x8c, 0x30,0x00,0x00,0x00,0x0c, \
    0x30,0x00,0x00,0x00,0x0c, 0x30,0x00,0x00,0x00,0x0c, \
    0x0f,0xff,0xff,0xff,0xf0, 0x0f,0xff,0xff,0xff,0xf0, \
    0x00,0x3e,0x00,0x7c,0x00, 0x00,0x7c,0x00,0x3e,0x00, \
    0x00,0xf8,0x00,0x1f,0x00, 0x01,0xf0,0x00,0x0f,0x80, \
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00

#define DEFINE_SCREEN_IMAGE(name, blue, green, red) \
    static const uint8_t name##_data[] = { \
        0x00,0x00,0x00,0x00, blue,green,red,0xff, SCREEN_ICON_PIXELS \
    }; \
    static const lv_image_dsc_t name = { \
        {LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_I1, 0, 40, 40, 5, 0}, \
        sizeof(name##_data), name##_data, nullptr, nullptr \
    }

DEFINE_SCREEN_IMAGE(kScreenCyan,    0xff, 0xe5, 0x00);
DEFINE_SCREEN_IMAGE(kScreenYellow,  0x00, 0xea, 0xff);
DEFINE_SCREEN_IMAGE(kScreenPink,    0x71, 0x3d, 0xff);
DEFINE_SCREEN_IMAGE(kScreenMint,    0xae, 0xf0, 0x69);
DEFINE_SCREEN_IMAGE(kScreenOrange,  0x00, 0x91, 0xff);
DEFINE_SCREEN_IMAGE(kScreenPurple,  0xf9, 0x00, 0xd5);
DEFINE_SCREEN_IMAGE(kScreenLime,    0x03, 0xff, 0x76);
DEFINE_SCREEN_IMAGE(kScreenWhite,   0xff, 0xff, 0xff);

const lv_image_dsc_t *const kScreenImages[] = {
    &kScreenCyan, &kScreenYellow, &kScreenPink, &kScreenMint,
    &kScreenOrange, &kScreenPurple, &kScreenLime, &kScreenWhite,
};
static_assert(sizeof(kScreenImages) / sizeof(kScreenImages[0]) == ScreensaverModel::COLOR_COUNT);

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_block = nullptr;
lv_timer_t *s_timer = nullptr;
ScreensaverModel s_model;

void set_block_color(size_t color_index)
{
    lv_image_set_src(s_block, kScreenImages[color_index]);
}

void reset_animation_period()
{
    if (s_timer) lv_timer_set_period(s_timer, kIdleCheckMs);
}

void block_delete_cb(lv_event_t *event) noexcept
{
    try {
        if (!event || !screensaver_delete_is_tracked(
                lv_event_get_target(event), lv_event_get_current_target(event), s_block))
            return;
        s_block = nullptr;
        if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        s_model.deactivate();
        reset_animation_period();
    } catch (...) {
        s_block = nullptr;
        reset_animation_period();
        try {
            s_model.deactivate();
        } catch (...) {
        }
    }
}

void overlay_delete_cb(lv_event_t *event) noexcept
{
    try {
        if (!event || !screensaver_delete_is_tracked(
                lv_event_get_target(event), lv_event_get_current_target(event), s_overlay))
            return;
        s_overlay = nullptr;
        s_block = nullptr;
        s_model.deactivate();
        reset_animation_period();
    } catch (...) {
        s_overlay = nullptr;
        s_block = nullptr;
        reset_animation_period();
        try {
            s_model.deactivate();
        } catch (...) {
        }
    }
}

int timeout_seconds() noexcept
{
    try {
#if defined(HAL_PLATFORM_SDL)
    if (const char *override_value = std::getenv("EMU_SCREENSAVER_TIMEOUT_SECONDS")) {
        char *end = nullptr;
        const long seconds = std::strtol(override_value, &end, 10);
        if (end != override_value && *end == '\0' && seconds > 0 && seconds <= 3600)
            return static_cast<int>(seconds);
    }
#endif
    bool succeeded = false;
    std::string response;
    cp0_signal_config_api({"GetInt", "dark_time", "30"},
                          [&](int code, std::string data) {
                              succeeded = code == 0;
                              response = std::move(data);
                          });
    return screensaver_timeout_from_config(succeeded, response);
    } catch (...) {
        return screensaver_timeout_from_config(false, {});
    }
}

void create_objects()
{
    lv_display_t *display = lv_display_get_default();
    if (!display)
        return;

    if (!s_overlay) {
        lv_obj_t *parent = lv_layer_top();
        if (!parent)
            return;
        s_overlay = lv_obj_create(parent);
        if (!s_overlay)
            return;
        lv_obj_add_event_cb(s_overlay, overlay_delete_cb, LV_EVENT_DELETE, nullptr);
        lv_obj_remove_style_all(s_overlay);
        lv_obj_set_pos(s_overlay, 0, 0);
        lv_obj_set_size(s_overlay,
                        lv_display_get_horizontal_resolution(display),
                        lv_display_get_vertical_resolution(display));
        lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    if (s_block) return;

    s_block = lv_image_create(s_overlay);
    if (!s_block) {
        lv_obj_delete(s_overlay);
        return;
    }
    lv_obj_add_event_cb(s_block, block_delete_cb, LV_EVENT_DELETE, nullptr);
    lv_obj_remove_style_all(s_block);
    lv_obj_set_size(s_block, ScreensaverModel::BLOCK_SIZE, ScreensaverModel::BLOCK_SIZE);
    set_block_color(0);
    lv_obj_clear_flag(s_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_block, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void stop_screensaver(bool was_active = false)
{
    if (s_overlay)
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    if (was_active || s_model.active())
        if (lv_obj_t *active_screen = lv_screen_active())
            lv_obj_invalidate(active_screen);

    s_model.deactivate();
    if (s_timer)
        lv_timer_set_period(s_timer, kIdleCheckMs);
}

void start_screensaver()
{
    create_objects();
    if (!s_overlay || !s_block)
        return;

    lv_display_t *display = lv_display_get_default();
    if (!display) return;
    const int width = lv_display_get_horizontal_resolution(display);
    const int height = lv_display_get_vertical_resolution(display);
    lv_obj_set_size(s_overlay, width, height);

    const ScreensaverFrame frame = s_model.activate(width, height, lv_tick_get());
    lv_obj_set_pos(s_block, frame.x, frame.y);
    set_block_color(frame.color_index);

    lv_obj_move_foreground(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_set_period(s_timer, kAnimationFrameMs);
    lv_timer_reset(s_timer);
}

void animate(uint32_t now)
{
    lv_display_t *display = lv_display_get_default();
    if (!display || !s_block)
        return;

    const ScreensaverFrame frame = s_model.advance(
        lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display), now);
    if (frame.color_changed)
        set_block_color(frame.color_index);
    lv_obj_set_pos(s_block, frame.x, frame.y);
}

void timer_cb(lv_timer_t *timer) noexcept
{
    try {
    if (!screensaver_timer_is_current(timer, s_timer)) return;
    if (!s_model.foreground() || LVGL_RUN_FLAGE != 1)
        return;

    const uint32_t now = lv_tick_get();
    if (s_model.active()) {
        animate(now);
        return;
    }

    const int seconds = timeout_seconds();
    if (s_model.should_activate(now, static_cast<uint32_t>(seconds) * 1000u, true))
        start_screensaver();
    } catch (...) {
        stop_screensaver();
    }
}

} // namespace

extern "C" void ui_screensaver_init(void)
{
    try {
    if (s_timer)
        return;
    create_objects();
    s_model.set_foreground(true, lv_tick_get());
    s_timer = lv_timer_create(timer_cb, kIdleCheckMs, nullptr);
    } catch (...) {
        if (s_timer) {
            lv_timer_delete(s_timer);
            s_timer = nullptr;
        }
        s_model.set_foreground(false, 0);
    }
}

extern "C" void ui_screensaver_deinit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    if (s_overlay)
        lv_obj_delete(s_overlay);
    s_overlay = nullptr;
    s_block = nullptr;
    s_model.reset(0);
    s_model.set_foreground(false, 0);
}

extern "C" int ui_screensaver_filter_key(const struct key_item *item)
{
    try {
    if (!item)
        return 0;

    const bool was_active = s_model.active();
    const bool consumed = s_model.filter_key(
        item->key_code, item->key_state == KBD_KEY_RELEASED, lv_tick_get());
    if (was_active && consumed) stop_screensaver(true);
    return consumed ? 1 : 0;
    } catch (...) {
        stop_screensaver();
        return 0;
    }
}

extern "C" void ui_screensaver_set_foreground(int foreground)
{
    try {
    stop_screensaver();
    s_model.set_foreground(foreground != 0, lv_tick_get());
    } catch (...) {
        stop_screensaver();
        s_model.set_foreground(false, 0);
    }
}
