#include "../main/ui/esc_ui_watchdog.h"

#include <cassert>

int main()
{
    assert(!esc_ui_watchdog_should_recover(false, true, 10000));
    assert(!esc_ui_watchdog_should_recover(true, false, 10000));
    assert(!esc_ui_watchdog_should_recover(true, true, 3499));
    assert(esc_ui_watchdog_should_recover(true, true, 3500));
}
