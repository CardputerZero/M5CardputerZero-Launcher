/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * ui_global_hint.cpp
 *
 * Global shortcut dispatcher and transient on-screen hints.
 *
 * Behavior:
 *   (a) ESC held continuously for >= 1.5s -> show
 *       "Hold ESC 3s to return home" for ~1.5s. Short taps (released
 *       before 1.5s) show nothing, so a quick "back" press inside
 *       an app no longer flashes the return-home toast.
 *   (b) Single press of SHIFT (Aa / KEY_LEFTSHIFT) or SYM (physical
 *       "SYM" key on the M5 CardputerZero; currently best-effort mapped)
 *       -> show "Double-tap to lock" for ~1.5s.
 *
 *   Fn key is intentionally NOT hinted (no lock feature yet).
 *
 * The toast object is created lazily on first call as a child of
 * lv_layer_top(), so it floats above any screen. It is never deleted
 * (to avoid delete-inside-event issues); visibility is toggled via
 * LV_OBJ_FLAG_HIDDEN. A single lv_timer performs the auto-hide; each
 * new trigger resets the timer's remaining time.
 */

#include "ui_global_hint.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"
#include "hal_lvgl_bsp.h"
#include "launcher_platform.hpp"
#include "launcher_media_controls.h"
#include "launcher_media_osd.h"
#include "launcher_toast.h"
#include "esc_hold_hint_controller.h"
#include "model/global_hint_policy.hpp"

#include "input_keys.h"

#include <string>
#include <utility>

#define MEDIA_OSD_STEP      5

static void show_hint(const char *text)
{
    esc_hold_hint_controller().clear_hint_ownership();
    launcher_toast().show(text);
}

static int ensure_screenshot_dir(const char *scr_dir)
{
    int result = -1;
    try {
        cp0_signal_filesystem_api(
            {"EnsureDirForUser", scr_dir ? scr_dir : ""},
            [&](int code, std::string) { result = code; });
    } catch (...) {
    }
    return result;
}

namespace ui_global_hint {

void shutdown()
{
    esc_hold_hint_controller().shutdown();
    launcher_toast().shutdown();
    launcher_media_osd().shutdown();
}

void on_key(const struct key_item *elm) noexcept
{
    try {
    if (elm == NULL) return;

    if (esc_hold_hint_controller().handle(elm))
        return;

    static const GlobalHintPolicy policy;
    const GlobalHintAction action = policy.action_for({
        elm->key_code,
        elm->sym_name,
        elm->key_state == KBD_KEY_PRESSED,
        elm->key_state == KBD_KEY_REPEATED,
        (elm->mods & KBD_MOD_CTRL) != 0,
        (elm->mods & KBD_MOD_ALT) != 0,
    });

    switch (action) {
        case GlobalHintAction::BRIGHTNESS_UP:
        case GlobalHintAction::BRIGHTNESS_DOWN: {
            const int delta = action == GlobalHintAction::BRIGHTNESS_UP ? MEDIA_OSD_STEP
                                                                       : -MEDIA_OSD_STEP;
            const int pct = launcher_media_controls::adjust_brightness(delta);
            launcher_media_osd().show_level("Brightness", LV_SYMBOL_TINT, pct);
            return;
        }
        case GlobalHintAction::VOLUME_UP:
        case GlobalHintAction::VOLUME_DOWN: {
            const int delta = action == GlobalHintAction::VOLUME_UP ? MEDIA_OSD_STEP
                                                                   : -MEDIA_OSD_STEP;
            const int pct = launcher_media_controls::adjust_volume(delta);
            launcher_media_osd().show_level(
                "Volume", pct == 0 ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX, pct);
            return;
        }
        case GlobalHintAction::TOGGLE_MUTE:
            launcher_media_osd().show_mute(launcher_media_controls::toggle_mute());
            return;
        case GlobalHintAction::TAKE_SCREENSHOT: {
            const std::string scr_dir = launcher_platform::path("home_dir") + "/Screenshots";
            const int ensure_result = ensure_screenshot_dir(scr_dir.c_str());
            int result = -1;
            std::string saved_path;
            if (GlobalHintScreenshotPolicy::should_save(ensure_result))
                cp0_signal_screenshot_api({"Save", scr_dir}, [&](int code, std::string data) {
                    result = code;
                    if (code == 0) saved_path = std::move(data);
                });
            if (ensure_result == 0 && result == 0 && !saved_path.empty()) {
                const std::string message = GlobalHintScreenshotPolicy::saved_file_message(
                    saved_path, launcher_platform::path("home_dir"));
                show_hint(message.c_str());
            } else {
                show_hint(GlobalHintScreenshotPolicy::result_message(ensure_result, result));
            }
            return;
        }
        case GlobalHintAction::SHOW_LOCK_HINT:
            show_hint("Double-tap to lock");
            return;
        case GlobalHintAction::NONE:
        default:
            return;
    }
    } catch (...) {
    }
}

} // namespace ui_global_hint

extern "C" void ui_global_hint_on_key(const struct key_item *elm) noexcept
{
    ui_global_hint::on_key(elm);
}
