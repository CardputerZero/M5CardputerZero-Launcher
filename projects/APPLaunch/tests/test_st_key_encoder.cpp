/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/st_key_encoder.hpp"
#include "../main/ui/model/st_page_contract.hpp"

#include "input_keys.h"
#include "keyboard_input.h"

#include <cassert>
#include <string>

int main()
{
    static_assert(noexcept(st_callback_failure_state()));
    constexpr auto callback_failure = st_callback_failure_state();
    static_assert(!callback_failure.terminal_active);
    static_assert(callback_failure.waiting_key_to_exit);
    static_assert(callback_failure.home_hold_status == 0);

    int child_status = 7;
    assert(st_parse_child_status("0", child_status) && child_status == 0);
    assert(st_parse_child_status("-15", child_status) && child_status == -15);
    assert(!st_parse_child_status("", child_status));
    assert(!st_parse_child_status("12 exited", child_status));
    assert(!st_parse_child_status(" 12", child_status));
    assert(!st_parse_child_status("99999999999999999999", child_status));

    int container = 0;
    int canvas = 0;
    int cursor = 0;
    int renderer_part = 0;
    assert(st_page_renderer_ready(&container, &canvas, &cursor));
    assert(!st_page_renderer_ready(nullptr, &canvas, &cursor));
    assert(!st_page_renderer_ready(&container, nullptr, &cursor));
    assert(!st_page_renderer_ready(&container, &canvas, nullptr));
    assert(st_page_renderer_ready(&container, &canvas, &cursor, &renderer_part));
    assert(!st_page_renderer_ready(&container, &canvas, &cursor, nullptr));

    assert(STKeyEncoder::encode(KEY_ENTER, nullptr, false) == "\r");
    assert(STKeyEncoder::encode(KEY_KPENTER, nullptr, false) == "\r");
    assert(STKeyEncoder::encode(KEY_BACKSPACE, nullptr, false) ==
           std::string(1, static_cast<char>(0x7f)));
    assert(STKeyEncoder::encode(KEY_ESC, nullptr, false) == "\x1b");

    assert(STKeyEncoder::encode(KEY_UP, nullptr, false) == "\x1b[A");
    assert(STKeyEncoder::encode(KEY_DOWN, nullptr, false) == "\x1b[B");
    assert(STKeyEncoder::encode(KEY_RIGHT, nullptr, false) == "\x1b[C");
    assert(STKeyEncoder::encode(KEY_LEFT, nullptr, false) == "\x1b[D");
    assert(STKeyEncoder::encode(KEY_UP, nullptr, true) == "\x1bOA");
    assert(STKeyEncoder::encode(KEY_DOWN, nullptr, true) == "\x1bOB");
    assert(STKeyEncoder::encode(KEY_RIGHT, nullptr, true) == "\x1bOC");
    assert(STKeyEncoder::encode(KEY_LEFT, nullptr, true) == "\x1bOD");
    // Insert is unsupported by UISTPage and must not write ESC[2~ to the PTY.
    assert(STKeyEncoder::encode(KEY_INSERT, "\033[2~", false).empty());

    assert(STKeyEncoder::encode(KEY_A, "a", false) == "a");
    assert(STKeyEncoder::encode(KEY_A, "\xe4\xbd\xa0", false) == "\xe4\xbd\xa0");
    assert(STKeyEncoder::encode(KEY_A, nullptr, false).empty());
    assert(STKeyEncoder::encode(KEY_A, "", false).empty());

    const std::string maximum(STKeyEncoder::MAX_TEXT_BYTES, 'x');
    const std::string too_long(STKeyEncoder::MAX_TEXT_BYTES + 1, 'x');
    assert(STKeyEncoder::encode(KEY_A, maximum.c_str(), false) == maximum);
    assert(STKeyEncoder::encode(KEY_A, too_long.c_str(), false).empty());

    // A recognized control key never falls through to the supplied text.
    assert(STKeyEncoder::encode(KEY_ENTER, "ignored", false) == "\r");

    assert(STKeyEncoder::should_forward_event(KEY_ENTER, KBD_KEY_PRESSED));
    assert(!STKeyEncoder::should_forward_event(KEY_ENTER, KBD_KEY_REPEATED));
    assert(!STKeyEncoder::should_forward_event(KEY_ENTER, KBD_KEY_RELEASED));
    assert(STKeyEncoder::should_forward_event(KEY_KPENTER, KBD_KEY_PRESSED));
    assert(!STKeyEncoder::should_forward_event(KEY_KPENTER, KBD_KEY_REPEATED));
    assert(!STKeyEncoder::should_forward_event(KEY_INSERT, KBD_KEY_PRESSED));
    assert(!STKeyEncoder::should_forward_event(KEY_INSERT, KBD_KEY_REPEATED));
    assert(STKeyEncoder::should_forward_event(KEY_A, KBD_KEY_PRESSED));
    assert(STKeyEncoder::should_forward_event(KEY_A, KBD_KEY_REPEATED));
    assert(!STKeyEncoder::should_forward_event(KEY_A, KBD_KEY_RELEASED));
    assert(!STKeyEncoder::should_forward_event(KEY_A, 99));

    assert(STKeyEncoder::scrollback_direction(KEY_PAGEUP, 0, false) == 0);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, KBD_MOD_CTRL, false) == 0);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, KBD_MOD_ALT, false) == 0);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEUP, KBD_MOD_SHIFT, false) == 1);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, KBD_MOD_SHIFT, false) == -1);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEUP, 0, true) == 1);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, 0, true) == -1);

    constexpr uint32_t ctrl_alt = KBD_MOD_CTRL | KBD_MOD_ALT;
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEUP, ctrl_alt, false) == 1);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, ctrl_alt, false) == -1);
    assert(STKeyEncoder::scrollback_direction(KEY_A, ctrl_alt, false) == 0);
    return 0;
}
