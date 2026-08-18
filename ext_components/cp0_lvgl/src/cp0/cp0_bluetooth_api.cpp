#include "cp0_bluetooth_backend.hpp"
#include "../cp0_init_once.hpp"
#include "../cp0_bluetooth_api_contract.hpp"

#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct BluetoothBackendSession {
    std::atomic<bool> running{true};
    std::atomic<bool> scanning{false};
    std::mutex mutex;
    std::future<cp0_bt_status_t> status_future;
    std::thread scan_thread;
    std::vector<std::future<void>> futures;
};

std::mutex g_sessions_mutex;
std::unordered_map<uint64_t, std::shared_ptr<BluetoothBackendSession>> g_sessions;
std::atomic<uint64_t> g_next_session_id{1};

std::string encode_status(const cp0_bt_status_t &status)
{
    std::ostringstream output;
    output << status.powered << '\t'
           << cp0::bluetooth::sanitize_wire_field(status.address) << '\t'
           << status.discoverable << '\t'
           << cp0::bluetooth::sanitize_wire_field(status.alias);
    return output.str();
}

std::string encode_devices(const cp0_bt_device_t *devices, int count)
{
    std::ostringstream output;
    for(int i = 0; devices && i < count; ++i)
    {
        output << cp0::bluetooth::sanitize_wire_field(devices[i].address) << '\t'
               << devices[i].rssi << '\t'
               << devices[i].connected << '\t' << devices[i].paired << '\t'
               << devices[i].trusted << '\t'
               << cp0::bluetooth::sanitize_wire_field(devices[i].name) << '\n';
    }
    return output.str();
}

void report(const std::function<void(int, std::string)> &callback, int code, const std::string &data)
{
    cp0::bluetooth::invoke_callback(callback, code, data);
}

template <typename Loader>
cp0::bluetooth::Reply load_devices(int requested_count, Loader loader)
{
    std::vector<cp0_bt_device_t> devices(static_cast<size_t>(requested_count));
    int count = loader(devices.empty() ? nullptr : devices.data(), static_cast<int>(devices.size()));
    return {count, encode_devices(devices.data(), count)};
}

std::shared_ptr<BluetoothBackendSession> find_session(uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    const auto found = g_sessions.find(session_id);
    return found == g_sessions.end() ? nullptr : found->second;
}

void start_status_get(uint64_t session_id,
                      std::shared_ptr<BluetoothBackendSession> session,
                      const std::function<void(int, std::string)> &callback)
{
    std::future<void> future = std::async(std::launch::async, [session, callback]() {
        const cp0_bt_status_t current = cp0_bluetooth_backend::status();
        std::lock_guard<std::mutex> lock(session->mutex);
        if (!session->running) return;
        report(callback, 0, encode_status(current));
    });
    std::lock_guard<std::mutex> lock(session->mutex);
    if (!session->running) return;
    session->futures.push_back(std::move(future));
}

void start_list_get(uint64_t session_id,
                    std::shared_ptr<BluetoothBackendSession> session,
                    bool connected_only,
                    const std::function<void(int, std::string)> &callback)
{
    std::future<void> future = std::async(std::launch::async,
        [session, connected_only, callback]() {
            const auto reply = load_devices(CP0_BT_DEVICE_MAX,
                [connected_only](cp0_bt_device_t *out, int count) {
                    return cp0_bluetooth_backend::list(out, count, connected_only);
                });
            std::lock_guard<std::mutex> lock(session->mutex);
            if (!session->running) return;
            report(callback, reply.code, reply.data);
        });
    std::lock_guard<std::mutex> lock(session->mutex);
    if (!session->running) return;
    session->futures.push_back(std::move(future));
}

void start_scan(uint64_t session_id,
                std::shared_ptr<BluetoothBackendSession> session,
                const std::function<void(int, std::string)> &callback)
{
    if (session->scanning.exchange(true))
        return;
    try {
        session->scan_thread = std::thread([session, callback]() {
            while (session->scanning.load()) {
                const auto reply = load_devices(CP0_BT_DEVICE_MAX,
                    [](cp0_bt_device_t *out, int count) {
                        return cp0_bluetooth_backend::scan(out, count);
                    });
                {
                    std::lock_guard<std::mutex> lock(session->mutex);
                    if (!session->running || !session->scanning.load()) break;
                    report(callback, reply.code, reply.data);
                }
                if (!session->scanning.load())
                    break;
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        });
    } catch (...) {
        session->scanning.store(false);
    }
}

uint64_t session_init()
{
    auto session = std::make_shared<BluetoothBackendSession>();
    session->running.store(true);
    session->scanning.store(false);
    try {
        session->status_future = std::async(std::launch::async, []() {
            return cp0_bluetooth_backend::status();
        });
    } catch (...) {
    }
    const uint64_t session_id = g_next_session_id.fetch_add(1, std::memory_order_relaxed);
    if (session_id == 0)
        return 0;
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    g_sessions.emplace(session_id, std::move(session));
    return session_id;
}

void session_deinit(uint64_t session_id,
                    std::shared_ptr<BluetoothBackendSession> session,
                    const std::function<void(int, std::string)> &callback)
{
    {
        std::lock_guard<std::mutex> lock(session->mutex);
        session->running.store(false);
        session->scanning.store(false);
    }
    if (session->scan_thread.joinable())
        session->scan_thread.join();
    if (session->status_future.valid())
        session->status_future.wait();
    std::vector<std::future<void>> pending;
    {
        std::lock_guard<std::mutex> lock(session->mutex);
        pending.swap(session->futures);
    }
    for (auto &future : pending)
        if (future.valid())
            future.wait();
    {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        const auto found = g_sessions.find(session_id);
        if (found != g_sessions.end() && found->second == session)
            g_sessions.erase(found);
    }
    report(callback, 0, "ok");
}

void api_call(std::list<std::string> args, std::function<void(int, std::string)> callback)
{
    using namespace cp0_bluetooth_backend;
    cp0::bluetooth::Request request;
    if (!cp0::bluetooth::parse_request(args, request)) {
        report(callback, -1, "invalid bt api request");
        return;
    }
    using cp0::bluetooth::Command;
    if (request.command == Command::SessionInit) {
        const uint64_t session_id = session_init();
        report(callback, session_id == 0 ? -1 : 0,
               session_id == 0 ? "session init failed" : std::to_string(session_id));
        return;
    }
    if (request.command == Command::SessionDeinit) {
        std::shared_ptr<BluetoothBackendSession> session = find_session(request.session_id);
        if (!session) {
            report(callback, -1, "invalid bt session");
            return;
        }
        session_deinit(request.session_id, std::move(session), callback);
        return;
    }
    if (request.command == Command::StatusGet) {
        std::shared_ptr<BluetoothBackendSession> session = find_session(request.session_id);
        if (!session) {
            report(callback, -1, "invalid bt session");
            return;
        }
        start_status_get(request.session_id, std::move(session), callback);
        return;
    }
    if (request.command == Command::ConnectedListInit ||
        request.command == Command::ConnectedListDeinit) {
        report(callback, find_session(request.session_id) ? 0 : -1,
               find_session(request.session_id) ? "ok" : "invalid bt session");
        return;
    }
    if (request.command == Command::ConnectedListGet) {
        std::shared_ptr<BluetoothBackendSession> session = find_session(request.session_id);
        if (!session) {
            report(callback, -1, "invalid bt session");
            return;
        }
        start_list_get(request.session_id, std::move(session), true, callback);
        return;
    }
    if (request.command == Command::ScanOn) {
        std::shared_ptr<BluetoothBackendSession> session = find_session(request.session_id);
        if (!session) {
            report(callback, -1, "invalid bt session");
            return;
        }
        start_scan(request.session_id, std::move(session), callback);
        return;
    }
    if (request.command == Command::ScanOff) {
        std::shared_ptr<BluetoothBackendSession> session = find_session(request.session_id);
        if (!session) {
            report(callback, -1, "invalid bt session");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(session->mutex);
            session->scanning.store(false);
        }
        if (session->scan_thread.joinable())
            session->scan_thread.join();
        report(callback, 0, "ok");
        return;
    }
    cp0::bluetooth::invoke_backend(callback, [&]() -> cp0::bluetooth::Reply {
        if(request.command == Command::Status) return {0, encode_status(cp0_bluetooth_backend::status())};
        if(request.command == Command::Power) return {cp0_bluetooth_backend::set_power(request.value), {}};
        if(request.command == Command::Alias) return {cp0_bluetooth_backend::set_alias(request.text.c_str()), {}};
        if(request.command == Command::Discoverable) return {cp0_bluetooth_backend::set_discoverable(request.value), {}};
        if(request.command == Command::Scan) return load_devices(request.max_count, cp0_bluetooth_backend::scan);
        if(request.command == Command::DiscoveryStart) return {cp0_bluetooth_backend::start_discovery(), {}};
        if(request.command == Command::DiscoveryStop) return {cp0_bluetooth_backend::stop_discovery(), {}};
        if(request.command == Command::List)
            return load_devices(request.max_count, [](cp0_bt_device_t *out, int count) { return cp0_bluetooth_backend::list(out, count, false); });
        if(request.command == Command::ConnectedList)
            return load_devices(request.max_count, [](cp0_bt_device_t *out, int count) { return cp0_bluetooth_backend::list(out, count, true); });
        if(request.command == Command::Pair) return {cp0_bluetooth_backend::pair(request.text.c_str()), {}};
        if(request.command == Command::Connect) return {cp0_bluetooth_backend::connect(request.text.c_str()), {}};
        if(request.command == Command::Disconnect) return {cp0_bluetooth_backend::disconnect(request.text.c_str()), {}};
        return {cp0_bluetooth_backend::remove(request.text.c_str()), {}};
    });
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
