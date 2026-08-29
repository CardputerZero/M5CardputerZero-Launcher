#pragma once

#include <string_view>
#include <vector>

namespace settings_t12b::boot_actions {

enum class Action {
    Reboot,
    Shutdown,
    FactoryReset,
    RearmOobe,
};

enum class Operation {
    Reboot,
    Shutdown,
    RemoveLauncherSettings,
    TouchOobeMarker,
};

struct Presentation
{
    std::string_view label;
    std::string_view confirmation_title;
};

const Presentation &presentation(Action action);
std::vector<Operation> operation_plan(Action action);
bool may_continue(Operation completed_operation, bool succeeded);
const char *process_command(Operation operation) noexcept;
const char *filesystem_alias(Operation operation) noexcept;

} // namespace settings_t12b::boot_actions
