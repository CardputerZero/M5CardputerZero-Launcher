#include "sdl_bluetooth_settings.hpp"

#include "cp0_lvgl_app.h"
#include "hal/hal_settings.h"
#include "hal_lvgl_bsp.h"

#include "../cp0_app_internal_utils.h"
#include "../cp0_bluetooth_api_contract.hpp"
#include "../cp0_bluetooth_session.hpp"
#include "../cp0_signal_registration.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <list>
#include <string>
#include <vector>

namespace sdl_bluetooth_settings {
namespace {

using cp0::bluetooth::BackendOps;
using cp0::bluetooth::BluetoothSessionManager;
using cp0::bluetooth::Command;
using cp0::bluetooth::Reply;

cp0_bt_status_t convert_status(const hal_bt_status_t &source)
{
    cp0_bt_status_t result{};
    result.powered = source.powered;
    result.discoverable = source.discoverable;
    cp0_copy_string(result.address, sizeof(result.address), source.address);
    cp0_copy_string(result.alias, sizeof(result.alias), source.alias);
    return result;
}

int convert_devices(const hal_bt_device_t *source, int count, cp0_bt_device_t *out, int max,
                    bool connected_only)
{
    if (!out || max <= 0 || !source || count <= 0)
        return 0;
    // Never trust a backend that reports more entries than the output buffer
    // can hold; the old SDL implementation had the same clamp.
    if (count > max)
        count = max;
    int written = 0;
    for (int index = 0; index < count && written < max; ++index) {
        if (connected_only && !source[index].connected)
            continue;
        cp0_copy_string(out[written].name, sizeof(out[written].name), source[index].name);
        cp0_copy_string(out[written].address, sizeof(out[written].address), source[index].address);
        out[written].rssi = source[index].rssi;
        out[written].connected = source[index].connected;
        out[written].paired = source[index].paired;
        out[written].trusted = source[index].trusted;
        ++written;
    }
    return written;
}

BackendOps make_backend_ops()
{
    BackendOps ops;
    ops.status = [] { return convert_status(hal_bt_get_status()); };
    ops.set_power = [](int on) { return hal_bt_set_power(on); };
    ops.set_alias = [](const char *) { return 0; };
    ops.set_discoverable = [](int) { return 0; };
    ops.start_discovery = [] { return 0; };
    ops.stop_discovery = [] { return 0; };
    ops.list = [](cp0_bt_device_t *out, int max, bool connected_only) {
        std::vector<hal_bt_device_t> source(static_cast<size_t>(max > 0 ? max : 0));
        const int count = hal_bt_scan(source.data(), max > 0 ? max : 0);
        return convert_devices(source.data(), count, out, max, connected_only);
    };
    ops.pair = [](const char *) { return -1; };
    ops.connect = [](const char *) { return -1; };
    ops.disconnect = [](const char *) { return -1; };
    ops.remove = [](const char *) { return -1; };
    return ops;
}

BluetoothSessionManager &sessions()
{
    static BluetoothSessionManager manager;
    return manager;
}

template <typename Loader>
Reply load_devices(int requested_count, Loader loader)
{
    std::vector<cp0_bt_device_t> devices(static_cast<size_t>(requested_count));
    int count = loader(devices.empty() ? nullptr : devices.data(), static_cast<int>(devices.size()));
    return {count, cp0::bluetooth::encode_devices(devices.data(), count)};
}

const char *device_command_name(Command command)
{
    switch (command) {
    case Command::Pair: return "pair";
    case Command::Connect: return "connect";
    case Command::Disconnect: return "disconnect";
    case Command::Remove: return "remove";
    default: return "";
    }
}

void dispatch_legacy(const cp0::bluetooth::Request &request,
                     const std::function<void(int, std::string)> &callback)
{
    cp0::bluetooth::invoke_backend(callback, [&]() -> Reply {
        if (request.command == Command::Status)
            return {0, cp0::bluetooth::encode_status(convert_status(hal_bt_get_status()))};
        if (request.command == Command::Power)
            return {hal_bt_set_power(request.value), {}};
        if (request.command == Command::Alias || request.command == Command::Discoverable)
            return {0, {}};
        if (request.command == Command::Scan || request.command == Command::List ||
            request.command == Command::ConnectedList) {
            return load_devices(request.max_count,
                                [connected = request.command == Command::ConnectedList](
                                    cp0_bt_device_t *out, int max) {
                                    std::vector<hal_bt_device_t> source(static_cast<size_t>(max));
                                    int count = hal_bt_scan(source.data(), max);
                                    return convert_devices(source.data(), count, out, max,
                                                           connected);
                                });
        }
        if (request.command == Command::DiscoveryStart ||
            request.command == Command::DiscoveryStop)
            return {0, {}};
        return {-1, {}};
    });
}

void api_call(const std::list<std::string> &args,
              const std::function<void(int, std::string)> &callback)
{
    cp0::bluetooth::Request request;
    if (!cp0::bluetooth::parse_request(args, request)) {
        cp0::bluetooth::invoke_callback(callback, -1, "invalid bt api request");
        return;
    }

    if (request.command == Command::SessionInit) {
        cp0::bluetooth::invoke_callback(callback, 0, sessions().create(make_backend_ops()));
        return;
    }
    if (request.command == Command::SessionDeinit) {
        cp0::bluetooth::invoke_callback(callback,
                                        sessions().deinit(request.session_id) ? 0 : -1, {});
        return;
    }

    if (!request.has_session) {
        dispatch_legacy(request, callback);
        return;
    }

    auto session = sessions().get(request.session_id);
    if (!session) {
        cp0::bluetooth::invoke_callback(callback, -1, "unknown bluetooth session");
        return;
    }

    switch (request.command) {
    case Command::StatusGet:
        session->status_get(callback);
        return;
    case Command::Power:
        session->set_power(request.value, callback);
        return;
    case Command::Alias:
        session->set_alias(request.text, callback);
        return;
    case Command::Discoverable:
        session->set_discoverable(request.value, callback);
        return;
    case Command::Pair:
    case Command::Connect:
    case Command::Disconnect:
    case Command::Remove:
        session->device_command(device_command_name(request.command), request.text, callback);
        return;
    case Command::ConnectedListInit:
        session->connected_list_init(callback);
        return;
    case Command::ConnectedListGet:
        session->connected_list_get(callback);
        return;
    case Command::ConnectedListDeinit:
        session->connected_list_deinit(callback);
        return;
    case Command::ScanOn:
        session->scan_on(callback);
        return;
    case Command::ScanOff:
        session->scan_off(callback);
        return;
    default:
        cp0::bluetooth::invoke_callback(callback, -1, "invalid bluetooth session command");
        return;
    }
}

} // namespace

void register_api()
{
    static cp0::SignalRegistration<decltype(cp0_signal_bt_api)> registration;
    registration.replace(
        cp0_signal_bt_api,
        [](std::list<std::string> args,
           std::function<void(int, std::string)> callback) {
            api_call(args, callback);
        });
}

} // namespace sdl_bluetooth_settings
