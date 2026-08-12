#pragma once

#include "input_keys.h"
#include "keyboard_input.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_KEYBOARD_TEXT_REPEAT_DELAY_MS 500u
#define CP0_KEYBOARD_NAV_REPEAT_DELAY_MS 500u

static inline uint32_t cp0_keyboard_navigation_alias(uint32_t key)
{
    switch (key) {
    case KEY_F: return KEY_UP;
    case KEY_X: return KEY_DOWN;
    case KEY_Z: return KEY_LEFT;
    case KEY_C: return KEY_RIGHT;
    default: return key;
    }
}

static inline uint32_t cp0_keyboard_semantic_key(
    uint32_t physical_key, cp0_keyboard_input_context_t context)
{
    return context == KBD_INPUT_CONTEXT_TEXT
               ? physical_key
               : cp0_keyboard_navigation_alias(physical_key);
}

static inline uint32_t cp0_keyboard_repeat_delay_ms(
    uint32_t physical_key, cp0_keyboard_input_context_t context)
{
    const uint32_t semantic = cp0_keyboard_semantic_key(physical_key, context);
    return semantic == KEY_UP || semantic == KEY_DOWN ||
                   semantic == KEY_LEFT || semantic == KEY_RIGHT
               ? CP0_KEYBOARD_NAV_REPEAT_DELAY_MS
               : CP0_KEYBOARD_TEXT_REPEAT_DELAY_MS;
}

#ifdef __cplusplus
}
#endif
