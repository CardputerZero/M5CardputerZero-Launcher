#include "sdl_bluetooth_settings.hpp"

#include "cp0_lvgl_app.h"
#include "hal/hal_settings.h"
#include "hal_lvgl_bsp.h"

#include "../cp0_app_internal_utils.h"
#include "../cp0_bluetooth_api_contract.hpp"
#include "../cp0_signal_registration.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace sdl_bluetooth_settings {
namespace {

std::atomic_bool simulated_bt_discoverable{false};

class BluetoothSettings
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

    void api_call(const arg_t &args, const callback_t &callback)
    {
        cp0::bluetooth::Request request;
        if (!cp0::bluetooth::parse_request(args, request)) {
            cp0::bluetooth::invoke_callback(callback, -1, "invalid bt api request");
            return;
        }
        cp0::bluetooth::invoke_backend(callback, [&]() -> cp0::bluetooth::Reply {
            using cp0::bluetooth::Command;
            if (request.command == Command::Status) return {0, encode_status(status())};
            if (request.command == Command::Power) {
                const int result = hal_bt_set_power(request.value);
                if (result == 0 && request.value == 0)
                    simulated_bt_discoverable.store(false, std::memory_order_release);
                return {result, {}};
            }
            if (request.command == Command::Alias) return {0, {}};
            if (request.command == Command::Discoverable) {
                simulated_bt_discoverable.store(request.value != 0, std::memory_order_release);
                return {0, {}};
            }
            if (request.command == Command::Scan || request.command == Command::List ||
                request.command == Command::ConnectedList) {
                std::vector<cp0_bt_device_t> devices(static_cast<size_t>(request.max_count));
                int count = scan(devices.empty() ? nullptr : devices.data(),
                                 static_cast<int>(devices.size()));
                if (request.command == Command::ConnectedList)
                    count = filter_connected(devices.data(), count);
                return {count, encode_devices(devices.data(), count)};
            }
            if (request.command == Command::DiscoveryStart ||
                request.command == Command::DiscoveryStop)
                return {0, {}};
            return {-1, {}};
        });
    }

private:
    static cp0_bt_status_t status()
    {
        const hal_bt_status_t source = hal_bt_get_status();
        cp0_bt_status_t result{};
        result.powered = source.powered;
        result.discoverable = simulated_bt_discoverable.load(std::memory_order_acquire) ? 1 : 0;
        cp0_copy_string(result.address, sizeof(result.address), source.address);
        cp0_copy_string(result.alias, sizeof(result.alias), source.alias);
        return result;
    }

    static int scan(cp0_bt_device_t *output, int max_devices)
    {
        if (!output || max_devices <= 0) return hal_bt_scan(nullptr, 0);

        std::vector<hal_bt_device_t> source(static_cast<size_t>(max_devices));
        int count = std::min(hal_bt_scan(source.data(), max_devices), max_devices);
        for (int index = 0; index < count; ++index) {
            cp0_copy_string(output[index].name, sizeof(output[index].name), source[index].name);
            cp0_copy_string(output[index].address, sizeof(output[index].address),
                            source[index].address);
            output[index].rssi = source[index].rssi;
            output[index].connected = source[index].connected;
            output[index].paired = source[index].paired;
            output[index].trusted = source[index].trusted;
        }
        return count;
    }

    static int filter_connected(cp0_bt_device_t *devices, int count)
    {
        if (!devices || count <= 0) return 0;
        int output_count = 0;
        for (int index = 0; index < count; ++index) {
            if (!devices[index].connected) continue;
            if (output_count != index) devices[output_count] = devices[index];
            ++output_count;
        }
        return output_count;
    }

    static std::string encode_status(const cp0_bt_status_t &value)
    {
        std::ostringstream output;
        output << value.powered << '\t'
               << cp0::bluetooth::sanitize_wire_field(value.address) << '\t'
               << value.discoverable << '\t'
               << cp0::bluetooth::sanitize_wire_field(value.alias);
        return output.str();
    }

    static std::string encode_devices(const cp0_bt_device_t *devices, int count)
    {
        std::ostringstream output;
        for (int index = 0; devices && index < count; ++index) {
            output << cp0::bluetooth::sanitize_wire_field(devices[index].address) << '\t'
                   << devices[index].rssi << '\t'
                   << devices[index].connected << '\t' << devices[index].paired << '\t'
                   << devices[index].trusted << '\t'
                   << cp0::bluetooth::sanitize_wire_field(devices[index].name) << '\n';
        }
        return output.str();
    }
};

} // namespace

void register_api()
{
    auto bluetooth = std::make_shared<BluetoothSettings>();
    static cp0::SignalRegistration<decltype(cp0_signal_bt_api)> registration;
    registration.replace(
        cp0_signal_bt_api,
        [bluetooth](std::list<std::string> args,
                    std::function<void(int, std::string)> callback) {
            bluetooth->api_call(args, callback);
        });
}

} // namespace sdl_bluetooth_settings
