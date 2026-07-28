#include "cp0_keyboard_input_context.hpp"

#include <cassert>

namespace {
cp0_keyboard_input_context_t current = KBD_INPUT_CONTEXT_GAME;
}

extern "C" void cp0_keyboard_set_input_context(cp0_keyboard_input_context_t context)
{
    current = context;
}

extern "C" cp0_keyboard_input_context_t cp0_keyboard_get_input_context(void)
{
    return current;
}

int main()
{
    {
        Cp0KeyboardInputContextScope outer(KBD_INPUT_CONTEXT_NAVIGATION);
        assert(current == KBD_INPUT_CONTEXT_NAVIGATION);
        outer.update(KBD_INPUT_CONTEXT_TEXT);
        assert(current == KBD_INPUT_CONTEXT_TEXT);
        {
            Cp0KeyboardInputContextScope inner(KBD_INPUT_CONTEXT_GAME);
            assert(current == KBD_INPUT_CONTEXT_GAME);
        }
        assert(current == KBD_INPUT_CONTEXT_TEXT);
    }
    assert(current == KBD_INPUT_CONTEXT_GAME);
}
