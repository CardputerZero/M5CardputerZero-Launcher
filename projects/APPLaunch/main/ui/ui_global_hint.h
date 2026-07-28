/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * ui_global_hint.h
 *
 * Global launcher shortcut dispatcher for the launcher and sub-app pages.
 * Key classification lives in GlobalHintPolicy; this module executes the
 * resulting media, screenshot, and transient hint actions.
 *
 * Hooked from the cp0_lvgl keyboard dispatch after LV_EVENT_KEYBOARD
 * has been sent to the active screen.
 * The helper only READS elm — it never frees it.
 */
#ifndef UI_GLOBAL_HINT_H
#define UI_GLOBAL_HINT_H

struct key_item;

#ifdef __cplusplus
namespace ui_global_hint {
void on_key(const struct key_item *elm) noexcept;
void shutdown();
bool external_esc_hint_visible() noexcept;
void reset_external_esc_hint() noexcept;
}

extern "C" {
#endif

/* Call on every key_item dequeued from the keyboard queue.
 * Executes a matching global shortcut; a no-op for unrelated keys.
 */
#ifdef __cplusplus
void ui_global_hint_on_key(const struct key_item *elm) noexcept;
#else
void ui_global_hint_on_key(const struct key_item *elm);
#endif

#ifdef __cplusplus
}
#endif

#endif /* UI_GLOBAL_HINT_H */
