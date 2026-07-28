#include "../src/sdl/sdl_external_app_runner.hpp"
#include "cp0_esc_state.h"

#include <cassert>
#include <chrono>

namespace {
int hint_shown = 0;
int hint_hidden = 0;
std::chrono::steady_clock::time_point hint_shown_at;
}

extern "C" void ui_external_esc_hint(int visible)
{
    if (visible) {
        ++hint_shown;
        hint_shown_at = std::chrono::steady_clock::now();
    } else {
        ++hint_hidden;
    }
}

int main()
{
    assert(sdl_external_app_runner::run("exit 7") == 7);

    cp0_esc_state_write(1);
    const auto start = std::chrono::steady_clock::now();
    assert(sdl_external_app_runner::run("trap '' TERM; while :; do sleep 1; done") == -1);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(elapsed >= std::chrono::seconds(6));
    assert(elapsed < std::chrono::seconds(9));
    const auto hint_delay = hint_shown_at - start;
    assert(hint_shown == 1);
    assert(hint_hidden == 1);
    assert(hint_delay >= std::chrono::milliseconds(500));
    assert(hint_delay < std::chrono::milliseconds(800));
    assert(cp0_esc_state_read() == 0);
}
