/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class STKeyEncoder
{
public:
    static constexpr size_t MAX_TEXT_BYTES = 15;

    static std::string encode(uint32_t evdev_key, const char *utf8,
                              bool application_cursor_mode);
    static bool should_forward_event(uint32_t evdev_key, int key_state);
    static int scrollback_direction(uint32_t evdev_key, uint32_t modifiers,
                                    bool shift_down);
};
