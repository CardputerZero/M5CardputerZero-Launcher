#include "settings_t12b_launcher_model.hpp"

namespace settings_t12b::launcher {

bool state_after_write(bool previous, bool requested, bool succeeded) noexcept
{
    return succeeded ? requested : previous;
}

bool should_notify_registry(bool succeeded) noexcept
{
    return succeeded;
}

} // namespace settings_t12b::launcher
