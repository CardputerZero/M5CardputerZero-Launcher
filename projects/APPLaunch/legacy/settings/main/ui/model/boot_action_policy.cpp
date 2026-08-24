#include "boot_action_policy.hpp"

namespace boot_actions {
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

} // namespace boot_actions
