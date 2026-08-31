#include "../main/ui/settings/settings_adapter.hpp"

#include <eventpp/callbacklist.h>

#include <cassert>
#include <functional>
#include <list>
#include <string>
#include <utility>
#include <vector>

eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_filesystem_api;
eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_process_api;

namespace {

std::vector<std::list<std::string>> filesystem_calls;
std::vector<std::list<std::string>> process_calls;
bool fail_remove = false;

void install_fakes()
{
    cp0_signal_filesystem_api.append(
        [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
            filesystem_calls.push_back(arguments);
            const std::string command = arguments.empty() ? std::string() : arguments.front();
            if (command == "Path" && arguments.size() == 2) {
                callback(0, "/tmp/" + *std::next(arguments.begin()));
            } else if (command == "Remove" && fail_remove) {
                fail_remove = false;
                callback(-1, "remove failed");
            } else {
                callback(0, "");
            }
        });

    cp0_signal_process_api.append(
        [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
            process_calls.push_back(arguments);
            callback(0, "");
        });

}

void test_factory_reset_order()
{
    filesystem_calls.clear();
    process_calls.clear();
    std::vector<settings_t12b::BootResult> results;
    settings_t12b::BootActionController controller(
        [&results](const settings_t12b::BootResult &result) { results.push_back(result); });

    assert(controller.start(settings_t12b::boot_actions::Action::FactoryReset));
    assert(!controller.pending());
    assert(controller.poll() == 1);
    assert(results.size() == 1);
    assert(results[0].succeeded);
    assert(results[0].operation == settings_t12b::boot_actions::Operation::Reboot);
    assert(filesystem_calls.size() == 2);
    assert((filesystem_calls[0] == std::list<std::string>{"Path", "launcher_settings"}));
    assert((filesystem_calls[1] == std::list<std::string>{"Remove", "/tmp/launcher_settings"}));
    assert(process_calls.size() == 1);
    assert((process_calls[0] == std::list<std::string>{"Reboot"}));
}

void test_boot_failure_stops_plan()
{
    filesystem_calls.clear();
    process_calls.clear();
    fail_remove = true;
    std::vector<settings_t12b::BootResult> results;
    settings_t12b::BootActionController controller(
        [&results](const settings_t12b::BootResult &result) { results.push_back(result); });

    assert(controller.start(settings_t12b::boot_actions::Action::FactoryReset));
    assert(controller.poll() == 1);
    assert(results.size() == 1);
    assert(!results[0].succeeded);
    assert(results[0].operation == settings_t12b::boot_actions::Operation::RemoveLauncherSettings);
    assert(process_calls.empty());
}

void test_boot_confirmation_yes_no()
{
    process_calls.clear();
    auto no = settings_t12b::make_boot_confirmation_api(
        settings_t12b::boot_actions::Action::Reboot, false);
    no(SettingApiActivate, nullptr);
    assert(process_calls.empty());

    auto yes = settings_t12b::make_boot_confirmation_api(
        settings_t12b::boot_actions::Action::Reboot, true);
    yes(SettingApiActivate, nullptr);
    assert(process_calls.size() == 1);
    assert((process_calls[0] == std::list<std::string>{"Reboot"}));
}

} // namespace

int main()
{
    install_fakes();
    test_factory_reset_order();
    test_boot_failure_stops_plan();
    test_boot_confirmation_yes_no();
}
