#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "settings_system_api.hpp"

#include <cassert>
#include <cstring>
#include <functional>
#include <list>
#include <string>

eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_osinfo_api;
eventpp::EventQueue<CP0_C_EVENT_t, void(const std::list<std::string>)> cp0_task_queue;
uint32_t lv_c_event[2 * CP0_C_EVENT_END] = {};

namespace {

std::function<void(std::list<std::string>, std::function<void(int, std::string)>)> osinfo_handler;
int default_network_code = 0;
int ethernet_code = 0;
int account_code = 0;
int apt_background_calls = 0;
int launcher_background_calls = 0;

}

extern "C" int cp0_network_default_info_read(cp0_eth_info_t *info)
{
    if (!info) return -1;
    std::strcpy(info->ipv4, "192.0.2.10");
    std::strcpy(info->gateway, "192.0.2.1");
    std::strcpy(info->mac, "00:11:22:33:44:55");
    return default_network_code;
}

extern "C" int cp0_eth_info_read(cp0_eth_info_t *info)
{
    if (!info) return -1;
    std::strcpy(info->ipv4, "198.51.100.10");
    std::strcpy(info->gateway, "198.51.100.1");
    std::strcpy(info->mac, "66:77:88:99:aa:bb");
    return ethernet_code;
}

extern "C" int cp0_account_info_read(cp0_account_info_t *info)
{
    if (!info) return -1;
    std::strcpy(info->user, "alice");
    std::strcpy(info->hostname, "cardputer");
    return account_code;
}

extern "C" int cp0_system_apt_update_background(void)
{
    ++apt_background_calls;
    return 17;
}

extern "C" int cp0_system_update_launcher_background(void)
{
    ++launcher_background_calls;
    return 23;
}

int main()
{
    const auto registration = cp0_signal_osinfo_api.append(
        [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
            if (osinfo_handler) osinfo_handler(std::move(arguments), std::move(callback));
        });

    using namespace settings_system;

    NetworkInfo network;
    assert(read_network_default(network) == 0);
    assert(network.ip == "192.0.2.10");
    assert(network.gateway == "192.0.2.1");
    assert(network.mac == "00:11:22:33:44:55");

    NetworkInfo ethernet;
    assert(read_ethernet(ethernet) == 0);
    assert(ethernet.ip == "198.51.100.10");

    AccountInfo account;
    assert(read_account(account) == 0);
    assert(account.username == "alice");
    assert(account.hostname == "cardputer");

    int callback_count = 0;
    std::string callback_payload;
    osinfo_handler = [](std::list<std::string> arguments,
                        std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"NetworkDefaultInfoRead"});
        callback(0, "first");
        callback(0, "second");
    };
    request({"NetworkDefaultInfoRead"}, [&](int code, std::string payload) {
        assert(code == 0);
        ++callback_count;
        callback_payload = std::move(payload);
    });
    assert(callback_count == 1);
    assert(callback_payload == "first");

    osinfo_handler = [](std::list<std::string> arguments,
                        std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"UpdateLauncherState"});
        callback(0, "succeeded:1.2.3");
    };
    std::string launcher_state;
    assert(read_launcher_state(launcher_state) == 0);
    assert(launcher_state == "succeeded:1.2.3");

    osinfo_handler = [](std::list<std::string> arguments,
                        std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"UpdateLauncherStart"});
        callback(0, "job-7");
    };
    std::string job_id;
    assert(start_update(UpdateAction::UpdateLauncher, job_id) == 0);
    assert(job_id == "job-7");

    osinfo_handler = [](std::list<std::string> arguments,
                        std::function<void(int, std::string)> callback) {
        assert((arguments == std::list<std::string>{"UpdateJobStatus", "job-7"}));
        callback(0, "running");
    };
    std::string job_state;
    assert(update_status(job_id, job_state) == 0);
    assert(job_state == "running");

    osinfo_handler = [](std::list<std::string> arguments,
                        std::function<void(int, std::string)> callback) {
        assert((arguments == std::list<std::string>{"UpdateJobCancel", "job-7"}));
        callback(0, {});
    };
    assert(cancel_update(job_id) == 0);

    osinfo_handler = [](std::list<std::string> arguments,
                        std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"AptUpdateBackground"});
        callback(0, {});
    };
    request_background(UpdateAction::CheckSystem, [](int code, std::string payload) {
        assert(code == 0 && payload.empty());
    });
    assert(apt_update_background() == 17);
    assert(apt_background_calls == 1);

    osinfo_handler = [](std::list<std::string> arguments,
                        std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"UpdateLauncherBackground"});
        callback(0, {});
    };
    request_background(UpdateAction::UpdateLauncher, [](int code, std::string payload) {
        assert(code == 0 && payload.empty());
    });
    assert(update_launcher_background() == 23);
    assert(launcher_background_calls == 1);

    cp0_signal_osinfo_api.remove(registration);
    return 0;
}
