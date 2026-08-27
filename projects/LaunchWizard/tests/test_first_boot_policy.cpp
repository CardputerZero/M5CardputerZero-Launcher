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

    // Factory first boot: marker present, default username, no password ->
    // wizard.
    state.factory_marker = true;
    state.factory_username = true;
    passed &= expect_wizard(true, state, "factory unconfigured");

    // Factory image with the baked pi/raspberry password: the hash exists but
    // still verifies as the factory default, so the wizard must run.
    state.user_has_password = true;
    state.factory_credentials = true;
    passed &= expect_wizard(true, state, "factory baked default password");

    // Imager-provisioned device: factory marker still present, but the user
    // chose their own password -> skip straight to the launcher (bug #227).
    state.factory_credentials = false;
    passed &= expect_wizard(false, state, "factory imager-provisioned");

    // A renamed user always means the device was provisioned, even when no
    // password was set (e.g. Imager with SSH keys only).
    state.factory_username = false;
    state.user_has_password = false;
    passed &= expect_wizard(false, state, "renamed user without password");
    state.user_has_password = true;
    state.factory_credentials = true;
    passed &= expect_wizard(false, state, "renamed user keeps factory password");

    // Settings "Run Setup Wizard" re-arm always wins, even when configured.
    state.rearm_marker = true;
    passed &= expect_wizard(true, state, "re-arm on configured device");

    // No markers at all: fall back to the legacy piwiz/lightdm signal.
    state = {};
    passed &= expect_wizard(false, state, "no markers");
    state.legacy_piwiz_active = true;
    passed &= expect_wizard(true, state, "legacy piwiz");
    state.user_has_password = true;
    passed &= expect_wizard(true, state, "legacy piwiz ignores password");

    // Keyboard guide: both marker and binary are required. In particular, a
    // missing package must not consume the marker.
    passed &= launch_wizard::should_run_keyboard_guide(true, true);
    passed &= !launch_wizard::should_run_keyboard_guide(true, false);
    passed &= !launch_wizard::should_run_keyboard_guide(false, true);
    passed &= !launch_wizard::should_run_keyboard_guide(false, false);

    if (!passed)
        std::cerr << "first boot policy tests failed\n";
    return passed;
}
