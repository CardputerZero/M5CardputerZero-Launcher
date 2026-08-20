#include "first_boot_policy.h"

namespace launch_wizard {

bool should_run_wizard(const FirstBootState &state)
{
    if (state.rearm_marker)
        return true;
    if (state.factory_marker)
        return state.factory_username &&
               (!state.user_has_password || state.factory_credentials);
    return state.legacy_piwiz_active;
}

}  // namespace launch_wizard
