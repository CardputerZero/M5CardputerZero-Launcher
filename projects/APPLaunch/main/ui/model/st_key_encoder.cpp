/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "st_key_encoder.hpp"

#include "input_keys.h"
#include "keyboard_input.h"

#include <cstring>

std::string STKeyEncoder::encode(uint32_t evdev_key, const char *utf8,
                                 bool application_cursor_mode)
{
    switch (evdev_key) {
    case KEY_ENTER:
    case KEY_KPENTER: return "\r";
    case KEY_BACKSPACE: return std::string(1, static_cast<char>(0x7f));
    case KEY_ESC: return "\x1b";
    case KEY_UP: return application_cursor_mode ? "\x1bOA" : "\x1b[A";
    case KEY_DOWN: return application_cursor_mode ? "\x1bOB" : "\x1b[B";
    case KEY_RIGHT: return application_cursor_mode ? "\x1bOC" : "\x1b[C";
    case KEY_LEFT: return application_cursor_mode ? "\x1bOD" : "\x1b[D";
    default: break;
    }

    if (!utf8) return {};
    const size_t length = std::strlen(utf8);
    if (length == 0 || length > MAX_TEXT_BYTES) return {};
    return std::string(utf8, length);
}

bool STKeyEncoder::should_forward_event(uint32_t evdev_key, int key_state)
{
    if (key_state != KBD_KEY_PRESSED && key_state != KBD_KEY_REPEATED) return false;

    // Repeating Enter queues an additional shell submission for one physical press.
    if (evdev_key == KEY_ENTER || evdev_key == KEY_KPENTER)
        return key_state == KBD_KEY_PRESSED;
    return true;
}

int STKeyEncoder::scrollback_direction(uint32_t evdev_key, uint32_t modifiers,
                                       bool shift_down)
{
    const bool shift = shift_down || (modifiers & KBD_MOD_SHIFT) != 0;
    constexpr uint32_t LEGACY_MODIFIERS = KBD_MOD_CTRL | KBD_MOD_ALT;
    const bool ctrl_alt = (modifiers & LEGACY_MODIFIERS) == LEGACY_MODIFIERS;
    if (!shift && !ctrl_alt) return 0;

    if (evdev_key == KEY_PAGEUP) return 1;
    if (evdev_key == KEY_PAGEDOWN) return -1;
    return 0;
}
