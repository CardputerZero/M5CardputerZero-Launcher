#pragma once

#include "keyboard_input.h"

class Cp0KeyboardInputContextScope
{
public:
    explicit Cp0KeyboardInputContextScope(
        cp0_keyboard_input_context_t context = KBD_INPUT_CONTEXT_NAVIGATION)
        : previous_(cp0_keyboard_get_input_context())
    {
        cp0_keyboard_set_input_context(context);
    }

    ~Cp0KeyboardInputContextScope() { cp0_keyboard_set_input_context(previous_); }

    Cp0KeyboardInputContextScope(const Cp0KeyboardInputContextScope &) = delete;
    Cp0KeyboardInputContextScope &operator=(const Cp0KeyboardInputContextScope &) = delete;

    void update(cp0_keyboard_input_context_t context)
    {
        cp0_keyboard_set_input_context(context);
    }

private:
    cp0_keyboard_input_context_t previous_;
};
