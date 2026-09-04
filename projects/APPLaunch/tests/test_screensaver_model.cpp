#include "../main/ui/model/screensaver_model.hpp"
#include "../main/ui/model/screensaver_runtime_contract.hpp"

#include <cassert>
#include <cstdint>

int main()
{
    int timer = 0;
    int stale_timer = 0;
    assert(screensaver_timer_is_current(&timer, &timer));
    assert(!screensaver_timer_is_current(&stale_timer, &timer));
    assert(!screensaver_timer_is_current(nullptr, &timer));
    assert(screensaver_delete_is_tracked(&timer, &timer, &timer));
    assert(!screensaver_delete_is_tracked(&stale_timer, &timer, &timer));
    assert(!screensaver_delete_is_tracked(&timer, &stale_timer, &timer));
    assert(!screensaver_delete_is_tracked(&timer, &timer, &stale_timer));
    assert(!screensaver_delete_is_tracked(nullptr, &timer, &timer));
    assert(screensaver_timeout_from_config(true, "0") == 0);
    assert(screensaver_timeout_from_config(true, "10") == 10);
    assert(screensaver_timeout_from_config(true, "300") == 300);
    assert(screensaver_timeout_from_config(true, "garbage") == 30);
    assert(screensaver_timeout_from_config(true, "10junk") == 30);
    assert(screensaver_timeout_from_config(true, " 10") == 30);
    assert(screensaver_timeout_from_config(true, "+10") == 30);
    assert(screensaver_timeout_from_config(true, "99999999999999999999") == 30);
    assert(screensaver_timeout_from_config(true, "-1") == 30);
    assert(screensaver_timeout_from_config(false, "0") == 30);

    ScreensaverModel model;
    model.reset(1000);
    assert(!model.active());
    assert(!model.should_activate(30999, 30000, true));
    assert(model.should_activate(31000, 30000, true));
    assert(!model.should_activate(31000, 0, true));
    assert(!model.should_activate(31000, 30000, false));

    ScreensaverFrame frame = model.activate(240, 135, 31000);
    assert(model.active());
    assert(frame.x == 47);
    assert(frame.y == 28);
    assert(frame.color_index == 0);

    frame = model.advance(240, 135, 31040);
    assert(frame.x == 48);
    assert(frame.y == 29);
    assert(!frame.color_changed);

    for (int index = 0; index < 200 && !frame.color_changed; ++index)
        frame = model.advance(240, 135, 31080 + static_cast<uint32_t>(index) * 40);
    assert(frame.color_changed);
    assert(frame.color_index == 1);

    assert(model.filter_key(42, false, 40000));
    assert(!model.active());
    assert(model.filter_key(42, true, 40010));
    assert(!model.filter_key(42, false, 40020));
    assert(model.last_activity_tick() == 40020);

    model.activate(10, 10, 50000);
    frame = model.advance(10, 10, 50040);
    assert(frame.x == 0 && frame.y == 0);

    model.set_foreground(false, 60000);
    assert(!model.foreground());
    assert(!model.active());
    assert(!model.should_activate(90000, 30000, true));
    model.set_foreground(true, 90000);
    assert(!model.should_activate(119999, 30000, true));
    assert(model.should_activate(120000, 30000, true));

    model.reset(UINT32_MAX - 10);
    assert(model.should_activate(9, 20, true));
}
