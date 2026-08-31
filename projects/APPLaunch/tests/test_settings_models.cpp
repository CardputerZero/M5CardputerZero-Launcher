#include "../main/ui/settings/settings_about_help_model.hpp"
#include "../main/ui/settings/settings_boot_action_policy.hpp"
#include "../main/ui/settings/settings_launcher_model.hpp"

#include <cassert>
#include <string>

namespace {

struct Descriptor
{
    const char *label;
    const char *config_key;
    bool configurable;
    bool always_on;
};

} // namespace

int main()
{
    const Descriptor descriptors[] = {
        {"Hidden", "hidden", false, true},
        {"Calculator", "calculator", true, false},
        {nullptr, "missing-label", true, false},
        {"Missing key", nullptr, true, false},
    };
    const auto entries = settings_t12b::launcher::configurable_entries(
        descriptors,
        4,
        [](const Descriptor &descriptor) { return descriptor.always_on; });
    assert(entries.size() == 1);
    assert(entries[0].label == "Calculator");
    assert(entries[0].config_key == "calculator");
    assert(!entries[0].enabled);
    using settings_t12b::boot_actions::Action;
    using settings_t12b::boot_actions::Operation;
    assert(settings_t12b::boot_actions::presentation(Action::Reboot).confirmation_title == "Reboot?");
    assert(settings_t12b::boot_actions::presentation(Action::Shutdown).confirmation_title == "Shutdown?");
    const std::vector<Operation> factory_plan = {
        Operation::RemoveLauncherSettings,
        Operation::Reboot,
    };
    const std::vector<Operation> oobe_plan = {
        Operation::TouchOobeMarker,
        Operation::Reboot,
    };
    assert(settings_t12b::boot_actions::operation_plan(Action::FactoryReset) == factory_plan);
    assert(settings_t12b::boot_actions::operation_plan(Action::RearmOobe) == oobe_plan);
    assert(settings_t12b::boot_actions::may_continue(Operation::RemoveLauncherSettings, true));
    assert(!settings_t12b::boot_actions::may_continue(Operation::RemoveLauncherSettings, false));
    assert(!settings_t12b::boot_actions::may_continue(Operation::Reboot, true));
    assert(std::string(settings_t12b::boot_actions::process_command(Operation::Reboot)) == "Reboot");
    assert(std::string(settings_t12b::boot_actions::filesystem_alias(Operation::TouchOobeMarker)) ==
           "oobe_marker");

    const auto about = settings_t12b::about_help::about("1.2.3", "2026-08-24", "stable", "abc123");
    const auto help = settings_t12b::about_help::help();
    assert(!about.title.empty() && about.lines.size() >= 4);
    assert(!help.title.empty() && !help.lines.empty());
}
