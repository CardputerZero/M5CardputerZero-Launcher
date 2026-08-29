#include "cp0_sudo_prompt_key_policy.hpp"

#include <cassert>

int main()
{
    cp0_sudo::PromptConfirmKeyGate gate;

    // The release belonging to the key press that opened the prompt must not submit it.
    assert(!gate.release());

    gate.press();
    assert(gate.release());
    assert(!gate.release());

    gate.press();
    gate.reset();
    assert(!gate.release());
    return 0;
}
