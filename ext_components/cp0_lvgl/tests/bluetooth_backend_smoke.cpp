#include "../src/cp0/cp0_bluez_dbus_client.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <string>
#include <strings.h>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct Completion {
    int code = -1;
    std::string message;
};

Completion wait_for(const std::function<void(cp0_bluez_dbus::Completion)> &start,
                   std::chrono::seconds timeout)
{
    std::promise<Completion> promise;
    auto future = promise.get_future();
    start([&promise](int code, const std::string &message) {
        try {
            promise.set_value({code, message});
        } catch (...) {
        }
    });
    if (future.wait_for(timeout) != std::future_status::ready)
        return {-2, "timeout"};
    return future.get();
}

bool run_command(const char *name,
                 const std::function<void(cp0_bluez_dbus::Completion)> &start,
                 std::chrono::seconds timeout)
{
    const Completion result = wait_for(start, timeout);
    std::printf("%-18s code=%d message=%s\n", name, result.code,
                result.message.empty() ? "ok" : result.message.c_str());
    return result.code == 0;
}

void print_devices()
{
    cp0_bt_device_t devices[CP0_BT_DEVICE_MAX]{};
    const int count = cp0_bluez_dbus::list(devices, CP0_BT_DEVICE_MAX, false);
    std::printf("devices=%d\n", count);
    for (int index = 0; index < count; ++index) {
        std::printf("  %s name=%s rssi=%d paired=%d connected=%d trusted=%d\n",
                    devices[index].address, devices[index].name, devices[index].rssi,
                    devices[index].paired, devices[index].connected, devices[index].trusted);
    }
}

bool wait_for_device(const std::string &address, std::chrono::seconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        cp0_bt_device_t devices[CP0_BT_DEVICE_MAX]{};
        const int count = cp0_bluez_dbus::list(devices, CP0_BT_DEVICE_MAX, false);
        for (int index = 0; index < count; ++index) {
            if (strcasecmp(devices[index].address, address.c_str()) == 0)
                return true;
        }
        std::this_thread::sleep_for(1s);
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: %s PEER_MAC [--advertise-only]\n", argv[0]);
        return 2;
    }
    const std::string peer = argv[1];
    const bool advertise_only = argc == 3 && std::strcmp(argv[2], "--advertise-only") == 0;
    if (argc == 3 && !advertise_only) {
        std::fprintf(stderr, "unknown mode: %s\n", argv[2]);
        return 2;
    }
    cp0_bluez_dbus::initialize();
    cp0_bluez_dbus::set_agent_listener([](const cp0_bluez_dbus::AgentRequest &request) {
        std::printf("agent method=%s device=%s passkey=%s uuid=%s\n",
                    request.method.c_str(), request.device.c_str(), request.passkey.c_str(),
                    request.uuid.c_str());
        std::string response;
        if (request.method == "RequestPinCode")
            response = "0000";
        else if (request.method == "RequestPasskey")
            response = "000000";
        cp0_bluez_dbus::agent_reply(request.id, true, response);
    });

    bool ok = false;
    for (int attempt = 0; attempt < 10 && !ok; ++attempt) {
        const cp0_bt_status_t status = cp0_bluez_dbus::status();
        std::printf("status powered=%d discoverable=%d address=%s alias=%s\n",
                    status.powered, status.discoverable, status.address, status.alias);
        ok = status.address[0] != '\0';
        if (!ok) std::this_thread::sleep_for(1s);
    }
    if (!ok) {
        std::fprintf(stderr, "bluetooth adapter was not ready\n");
        cp0_bluez_dbus::shutdown();
        return 1;
    }

    ok = run_command("power on", [](auto completion) {
        cp0_bluez_dbus::set_power_async(1, std::move(completion));
    }, 20s);
    ok = run_command("discoverable on", [](auto completion) {
        cp0_bluez_dbus::set_discoverable_async(1, std::move(completion));
    }, 20s) && ok;
    if (advertise_only) {
        std::printf("advertising; press Ctrl-C to stop\n");
        while (true) std::this_thread::sleep_for(1h);
    }
    ok = run_command("start discovery", [](auto completion) {
        cp0_bluez_dbus::start_discovery_async(std::move(completion));
    }, 20s) && ok;
    if (!wait_for_device(peer, 20s)) {
        std::fprintf(stderr, "peer %s was not discovered\n", peer.c_str());
        print_devices();
        ok = false;
    } else {
        print_devices();
        run_command("stop discovery", [](auto completion) {
            cp0_bluez_dbus::stop_discovery_async(std::move(completion));
        }, 20s);
        ok = run_command("pair", [&peer](auto completion) {
            cp0_bluez_dbus::pair_async(peer.c_str(), std::move(completion));
        }, 75s) && ok;
        print_devices();
        ok = run_command("connect", [&peer](auto completion) {
            cp0_bluez_dbus::connect_async(peer.c_str(), std::move(completion));
        }, 40s) && ok;
        print_devices();
    }
    cp0_bluez_dbus::shutdown();
    return ok ? 0 : 1;
}
