#include "first_boot_policy.h"

#include <iostream>
#include <string>

namespace {

bool expect_wizard(bool expected, const launch_wizard::FirstBootState &state,
                   const std::string &label)
{
    const bool actual = launch_wizard::should_run_wizard(state);
    if (actual == expected)
        return true;
    std::cerr << "should_run_wizard mismatch (" << label << "): expected "
              << expected << ", got " << actual << "\n";
    return false;
}

}  // namespace

bool test_first_boot_policy()
{
    bool passed = true;
    launch_wizard::FirstBootState state;

    // Factory first boot: marker present -> wizard, regardless of whether the
    // account already has a password. Only apply_all() disables the service.
    state.factory_marker = true;
    passed &= expect_wizard(true, state, "factory marker");

    // Settings "Run Setup Wizard" re-arm always wins, even when configured.
    state.rearm_marker = true;
    passed &= expect_wizard(true, state, "re-arm on configured device");

    // No markers at all: fall back to the legacy piwiz/lightdm signal.
    state = {};
    passed &= expect_wizard(false, state, "no markers");
    state.legacy_piwiz_active = true;
    passed &= expect_wizard(true, state, "legacy piwiz");

    // Keyboard guide: needs both the marker and the installed binary; a
    // missing binary keeps the marker for a later boot.
    passed &= launch_wizard::should_run_keyboard_guide(true, true);
    passed &= !launch_wizard::should_run_keyboard_guide(true, false);
    passed &= !launch_wizard::should_run_keyboard_guide(false, true);
    passed &= !launch_wizard::should_run_keyboard_guide(false, false);

    if (!passed)
        std::cerr << "first boot policy tests failed\n";
    return passed;
}
