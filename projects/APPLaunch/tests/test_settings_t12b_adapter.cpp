#include "../main/ui/settings/settings_t12b_adapter.hpp"

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
eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_settings_api;

namespace {

std::vector<std::list<std::string>> filesystem_calls;
std::vector<std::list<std::string>> process_calls;
std::vector<std::list<std::string>> settings_calls;
bool gpio_value = false;
bool fail_next_gpio_set = false;
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

    cp0_signal_settings_api.append(
        [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
            settings_calls.push_back(arguments);
            const std::string command = arguments.empty() ? std::string() : arguments.front();
            if (command == "GpioGet") {
                callback(0, gpio_value ? "1" : "0");
                return;
            }
            if (command == "GpioSet" && arguments.size() == 3) {
                const bool requested = *std::next(arguments.begin(), 2) == "1";
                if (fail_next_gpio_set) {
                    fail_next_gpio_set = false;
                    callback(-1, "gpio set failed");
                    return;
                }
                gpio_value = requested;
                callback(0, "ok");
                return;
            }
            callback(-1, "unexpected settings command");
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

void test_gpio_write_and_rollback()
{
    settings_calls.clear();
    gpio_value = false;
    auto toggle = settings_t12b::make_ext_port_toggle_api(settings_t12b::extport::Port::Grove5V);

    bool enabled = true;
    toggle(SettingApiReadFlag, &enabled);
    assert(!enabled);
    toggle(SettingApiActivate, nullptr);
    assert(gpio_value);
    assert(settings_calls.size() == 4);
    assert((settings_calls[0] == std::list<std::string>{"GpioGet", "GROVE5V"}));
    assert((settings_calls[1] == std::list<std::string>{"GpioGet", "GROVE5V"}));
    assert((settings_calls[2] == std::list<std::string>{"GpioSet", "GROVE5V", "1"}));
    assert((settings_calls[3] == std::list<std::string>{"GpioGet", "GROVE5V"}));

    settings_calls.clear();
    fail_next_gpio_set = true;
    toggle(SettingApiActivate, nullptr);
    assert(gpio_value);
    assert(settings_calls.size() == 3);
    assert((settings_calls[0] == std::list<std::string>{"GpioGet", "GROVE5V"}));
    assert((settings_calls[1] == std::list<std::string>{"GpioSet", "GROVE5V", "0"}));
    assert((settings_calls[2] == std::list<std::string>{"GpioSet", "GROVE5V", "1"}));
}

} // namespace

int main()
{
    install_fakes();
    test_factory_reset_order();
    test_boot_failure_stops_plan();
    test_boot_confirmation_yes_no();
    test_gpio_write_and_rollback();
}
