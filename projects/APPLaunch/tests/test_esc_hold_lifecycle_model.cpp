#include "../main/ui/model/esc_hold_lifecycle_model.hpp"

#include <cassert>
#include <cstdint>

int main()
{
    EscHoldLifecycleModel model;
    assert(model.press(1000));
    assert(!model.press(1400));
    auto decision = model.poll(1499, true, true);
    assert(!decision.show_hint && !decision.force_home);
    decision = model.poll(1500, true, true);
    assert(decision.show_hint && !decision.force_home);
    model.clear_hint_ownership();
    decision = model.poll(2600, true, true);
    assert(!decision.show_hint);
    assert(!model.release());

    assert(model.press(5000));
    decision = model.poll(8000, true, true);
    assert(decision.show_hint && decision.force_home && decision.pause_timer);
    decision = model.poll(9000, true, true);
    assert(!decision.force_home);
    assert(model.release());

    assert(model.press(10000));
    decision = model.poll(10100, false, true);
    assert(decision.pause_timer && !model.holding());
    assert(model.press(10200));

    model.cancel();
    assert(model.press(UINT32_MAX - 1000));
    decision = model.poll(499, true, true);
    assert(decision.show_hint && !decision.force_home);
    decision = model.poll(1999, true, true);
    assert(decision.force_home);
    assert(model.release());

    assert(model.press(3000));
    decision = model.poll(3600, true, true, false);
    assert(!decision.show_hint && !decision.force_home && decision.pause_timer &&
           !model.holding());

    int timer = 0;
    int stale_timer = 0;
    assert(esc_hold_timer_is_current(&timer, &timer));
    assert(!esc_hold_timer_is_current(&stale_timer, &timer));
    assert(!esc_hold_timer_is_current(nullptr, &timer));
}
