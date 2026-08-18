#include "first_boot_policy.h"

namespace launch_wizard {

bool should_run_wizard(const FirstBootState &state)
{
    if (state.rearm_marker)
        return true;
    if (state.factory_marker)
        return !state.user_has_password || state.factory_credentials;
    return state.legacy_piwiz_active;
}

bool should_run_keyboard_guide(bool marker_present, bool binary_present)
{
    return marker_present && binary_present;
}

}  // namespace launch_wizard
