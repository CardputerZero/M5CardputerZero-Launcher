#include "../main/ui/model/boot_action_policy.hpp"

#include <cassert>
#include <vector>

using boot_actions::Action;
using boot_actions::Operation;

int main()
{
    const auto &reboot = boot_actions::presentation(Action::Reboot);
    assert(reboot.label == "Reboot");
    assert(reboot.confirmation_title == "Reboot?");
    assert(boot_actions::operation_plan(Action::Reboot) == std::vector<Operation>{Operation::Reboot});

    const auto &shutdown = boot_actions::presentation(Action::Shutdown);
    assert(shutdown.label == "Shutdown");
    assert(shutdown.confirmation_title == "Shutdown?");
    assert(boot_actions::operation_plan(Action::Shutdown) == std::vector<Operation>{Operation::Shutdown});

    const auto &factory_reset = boot_actions::presentation(Action::FactoryReset);
    assert(factory_reset.confirmation_title.empty());
    const std::vector<Operation> factory_plan = {
        Operation::RemoveLauncherSettings,
        Operation::Reboot,
    };
    assert(boot_actions::operation_plan(Action::FactoryReset) == factory_plan);

    const auto &rearm_oobe = boot_actions::presentation(Action::RearmOobe);
    assert(rearm_oobe.confirmation_title.empty());
    const std::vector<Operation> oobe_plan = {
        Operation::TouchOobeMarker,
        Operation::Reboot,
    };
    assert(boot_actions::operation_plan(Action::RearmOobe) == oobe_plan);

    assert(boot_actions::may_continue(Operation::RemoveLauncherSettings, true));
    assert(!boot_actions::may_continue(Operation::RemoveLauncherSettings, false));
    assert(boot_actions::may_continue(Operation::TouchOobeMarker, true));
    assert(!boot_actions::may_continue(Operation::TouchOobeMarker, false));
    assert(!boot_actions::may_continue(Operation::Reboot, true));
    assert(!boot_actions::may_continue(Operation::Shutdown, true));
}
