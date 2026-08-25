#include "settings_t12b_boot_action_policy.hpp"

namespace settings_t12b::boot_actions {
namespace {

constexpr Presentation kReboot{"Reboot", "Reboot?"};
constexpr Presentation kShutdown{"Shutdown", "Shutdown?"};
constexpr Presentation kFactoryReset{"Factory Reset", {}};
constexpr Presentation kRearmOobe{"Run Setup Again", {}};

} // namespace

const Presentation &presentation(Action action)
{
    switch (action) {
    case Action::Reboot:
        return kReboot;
    case Action::Shutdown:
        return kShutdown;
    case Action::FactoryReset:
        return kFactoryReset;
    case Action::RearmOobe:
        return kRearmOobe;
    }
    return kReboot;
}

std::vector<Operation> operation_plan(Action action)
{
    switch (action) {
    case Action::Reboot:
        return {Operation::Reboot};
    case Action::Shutdown:
        return {Operation::Shutdown};
    case Action::FactoryReset:
        return {Operation::RemoveLauncherSettings, Operation::Reboot};
    case Action::RearmOobe:
        return {Operation::TouchOobeMarker, Operation::Reboot};
    }
    return {};
}

bool may_continue(Operation completed_operation, bool succeeded)
{
    if (completed_operation == Operation::Reboot ||
        completed_operation == Operation::Shutdown)
        return false;
    return succeeded;
}

const char *process_command(Operation operation) noexcept
{
    switch (operation) {
    case Operation::Reboot:
        return "Reboot";
    case Operation::Shutdown:
        return "Shutdown";
    case Operation::RemoveLauncherSettings:
    case Operation::TouchOobeMarker:
        return nullptr;
    }
    return nullptr;
}

const char *filesystem_alias(Operation operation) noexcept
{
    switch (operation) {
    case Operation::RemoveLauncherSettings:
        return "launcher_settings";
    case Operation::TouchOobeMarker:
        return "oobe_marker";
    case Operation::Reboot:
    case Operation::Shutdown:
        return nullptr;
    }
    return nullptr;
}

} // namespace settings_t12b::boot_actions
