#include "cp0_bluetooth_backend.hpp"
#include "../cp0_init_once.hpp"
#include "../cp0_bluetooth_api_contract.hpp"
#include "../cp0_bluetooth_session.hpp"

#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <functional>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace {
using cp0::bluetooth::BackendOps;
using cp0::bluetooth::BluetoothSessionManager;
using cp0::bluetooth::Command;
using cp0::bluetooth::Reply;

void report(const std::function<void(int, std::string)> &callback, int code, const std::string &data)
{
    cp0::bluetooth::invoke_callback(callback, code, data);
}

BackendOps make_backend_ops()
{
    BackendOps ops;
    ops.status = [] { return cp0_bluetooth_backend::status(); };
    ops.set_power = [](int on) { return cp0_bluetooth_backend::set_power(on); };
    ops.set_alias = [](const char *alias) { return cp0_bluetooth_backend::set_alias(alias); };
    ops.set_discoverable = [](int on) { return cp0_bluetooth_backend::set_discoverable(on); };
    ops.start_discovery = [] { return cp0_bluetooth_backend::start_discovery(); };
    ops.stop_discovery = [] { return cp0_bluetooth_backend::stop_discovery(); };
    ops.list = [](cp0_bt_device_t *out, int max, bool connected_only) {
        return cp0_bluetooth_backend::list(out, max, connected_only);
    };
    ops.pair = [](const char *address) { return cp0_bluetooth_backend::pair(address); };
    ops.connect = [](const char *address) { return cp0_bluetooth_backend::connect(address); };
    ops.disconnect = [](const char *address) { return cp0_bluetooth_backend::disconnect(address); };
    ops.remove = [](const char *address) { return cp0_bluetooth_backend::remove(address); };
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
            return {0, cp0::bluetooth::encode_status(cp0_bluetooth_backend::status())};
        if (request.command == Command::Power)
            return {cp0_bluetooth_backend::set_power(request.value), {}};
        if (request.command == Command::Alias)
            return {cp0_bluetooth_backend::set_alias(request.text.c_str()), {}};
        if (request.command == Command::Discoverable)
            return {cp0_bluetooth_backend::set_discoverable(request.value), {}};
        if (request.command == Command::Scan)
            return load_devices(request.max_count, [](cp0_bt_device_t *out, int count) {
                return cp0_bluetooth_backend::scan(out, count);
            });
        if (request.command == Command::DiscoveryStart)
            return {cp0_bluetooth_backend::start_discovery(), {}};
        if (request.command == Command::DiscoveryStop)
            return {cp0_bluetooth_backend::stop_discovery(), {}};
        if (request.command == Command::List)
            return load_devices(request.max_count, [](cp0_bt_device_t *out, int count) {
                return cp0_bluetooth_backend::list(out, count, false);
            });
        if (request.command == Command::ConnectedList)
            return load_devices(request.max_count, [](cp0_bt_device_t *out, int count) {
                return cp0_bluetooth_backend::list(out, count, true);
            });
        if (request.command == Command::Pair)
            return {cp0_bluetooth_backend::pair(request.text.c_str()), {}};
        if (request.command == Command::Connect)
            return {cp0_bluetooth_backend::connect(request.text.c_str()), {}};
        if (request.command == Command::Disconnect)
            return {cp0_bluetooth_backend::disconnect(request.text.c_str()), {}};
        return {cp0_bluetooth_backend::remove(request.text.c_str()), {}};
    });
}

void api_call(std::list<std::string> args, std::function<void(int, std::string)> callback)
{
    cp0::bluetooth::Request request;
    if (!cp0::bluetooth::parse_request(args, request)) {
        report(callback, -1, "invalid bt api request");
        return;
    }

    // Session creation / destruction are the only commands that do not require
    // an existing session id up front.
    if (request.command == Command::SessionInit) {
        report(callback, 0, sessions().create(make_backend_ops()));
        return;
    }
    if (request.command == Command::SessionDeinit) {
        report(callback, sessions().deinit(request.session_id) ? 0 : -1, {});
        return;
    }

    if (!request.has_session) {
        dispatch_legacy(request, callback);
        return;
    }

    auto session = sessions().get(request.session_id);
    if (!session) {
        report(callback, -1, "unknown bluetooth session");
        return;
    }

    switch (request.command) {
    case Command::StatusGet:
        session->status_get(std::move(callback));
        return;
    case Command::Power:
        session->set_power(request.value, std::move(callback));
        return;
    case Command::Alias:
        session->set_alias(request.text, std::move(callback));
        return;
    case Command::Discoverable:
        session->set_discoverable(request.value, std::move(callback));
        return;
    case Command::Pair:
    case Command::Connect:
    case Command::Disconnect:
    case Command::Remove:
        session->device_command(device_command_name(request.command), request.text,
                                std::move(callback));
        return;
    case Command::ConnectedListInit:
        session->connected_list_init(std::move(callback));
        return;
    case Command::ConnectedListGet:
        session->connected_list_get(std::move(callback));
        return;
    case Command::ConnectedListDeinit:
        session->connected_list_deinit(std::move(callback));
        return;
    case Command::ScanOn:
        session->scan_on(std::move(callback));
        return;
    case Command::ScanOff:
        session->scan_off(std::move(callback));
        return;
    default:
        report(callback, -1, "invalid bluetooth session command");
        return;
    }
}

} // namespace

extern "C" void init_bluetooth(void)
{
    static cp0::InitOnce initialized;
    initialized.run([] {
        return static_cast<bool>(cp0_signal_bt_api.append(
            [](std::list<std::string> args,
               std::function<void(int, std::string)> callback) {
                api_call(std::move(args), std::move(callback));
            }));
    });
}
