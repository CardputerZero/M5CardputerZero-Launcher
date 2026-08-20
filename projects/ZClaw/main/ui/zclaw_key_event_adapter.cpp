#include "zclaw_key_event_adapter.h"

#include "keyboard_input.h"

#include <linux/input.h>

namespace zclaw {
namespace {

Key adapt_key(std::uint32_t key_code)
{
    switch (key_code) {
    case KEY_ENTER: return Key::Enter;
    case KEY_ESC: return Key::Escape;
    case KEY_BACKSPACE: return Key::Backspace;
    case KEY_DELETE: return Key::Delete;
    case KEY_TAB: return Key::Tab;
    case KEY_LEFT: return Key::Left;
    case KEY_RIGHT: return Key::Right;
    case KEY_UP: return Key::Up;
    case KEY_DOWN: return Key::Down;
    case KEY_A: return Key::A;
    case KEY_C: return Key::C;
    case KEY_F: return Key::F;
    case KEY_N: return Key::N;
    case KEY_X: return Key::X;
    case KEY_Y: return Key::Y;
    case KEY_Z: return Key::Z;
    default: return Key::Other;
    }
}

KeyPhase adapt_key_phase(int key_state)
{
    switch (key_state) {
    case KBD_KEY_PRESSED: return KeyPhase::Pressed;
    case KBD_KEY_REPEATED: return KeyPhase::Repeated;
    case KBD_KEY_RELEASED: return KeyPhase::Released;
    default: return KeyPhase::Unknown;
    }
}

}  // namespace

KeyEvent adapt_key_event(std::uint32_t key_code, int key_state,
                         std::uint32_t modifiers, const char *utf8)
{
    KeyEvent event;
    event.phase = adapt_key_phase(key_state);
    event.key = adapt_key(key_code);
    event.shift = (modifiers & KBD_MOD_SHIFT) != 0;
    event.fn = (modifiers & KBD_MOD_FN) != 0;
    if (utf8)
        event.text = utf8;
    return event;
}

}  // namespace zclaw
