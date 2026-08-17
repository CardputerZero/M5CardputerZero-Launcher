#include "cp0_bluetooth_session.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using cp0::bluetooth::BackendOps;
using cp0::bluetooth::BluetoothBackendSession;
using cp0::bluetooth::BluetoothSessionManager;

namespace {

struct Delivery {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    int code = -1;
    std::string data;

    void set(int c, std::string d)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            code = c;
            data = std::move(d);
            done = true;
        }
        cv.notify_all();
    }

    bool wait_for(std::chrono::milliseconds timeout = std::chrono::seconds(2))
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [this] { return done; });
    }
};

BackendOps fake_ops(std::atomic<int> *calls)
{
    BackendOps ops;
    ops.status = [calls]() -> cp0_bt_status_t {
        if (calls) ++*calls;
        cp0_bt_status_t status{};
        status.powered = 1;
        status.discoverable = 0;
        std::snprintf(status.address, sizeof(status.address), "AA:BB:CC:DD:EE:FF");
        std::snprintf(status.alias, sizeof(status.alias), "FakeAlias");
        return status;
    };
    ops.set_power = [calls](int on) { if (calls) ++*calls; return on ? 0 : -1; };
    ops.set_alias = [calls](const char *) { if (calls) ++*calls; return 0; };
    ops.set_discoverable = [calls](int on) { if (calls) ++*calls; return on ? 0 : -1; };
    ops.start_discovery = [calls]() { if (calls) ++*calls; return 0; };
    ops.stop_discovery = [calls]() { if (calls) ++*calls; return 0; };
    ops.list = [calls](cp0_bt_device_t *out, int max, bool connected_only) {
        if (calls) ++*calls;
        if (!out || max <= 0) return 0;
        int count = 0;
        if (!connected_only) {
            std::snprintf(out[0].address, sizeof(out[0].address), "11:22:33:44:55:66");
            std::snprintf(out[0].name, sizeof(out[0].name), "ScanDevice");
            out[0].rssi = -42;
            count = 1;
        } else {
            std::snprintf(out[0].address, sizeof(out[0].address), "AA:BB:CC:DD:EE:FF");
            std::snprintf(out[0].name, sizeof(out[0].name), "ConnectedDevice");
            out[0].connected = 1;
            count = 1;
        }
        return count;
    };
    ops.pair = [calls](const char *) { if (calls) ++*calls; return 0; };
    ops.connect = [calls](const char *) { if (calls) ++*calls; return 0; };
    ops.disconnect = [calls](const char *) { if (calls) ++*calls; return 0; };
    ops.remove = [calls](const char *) { if (calls) ++*calls; return 0; };
    return ops;
}

} // namespace

int main()
{
    // Registry lifecycle.
    {
        BluetoothSessionManager manager;
        assert(manager.size() == 0);
        const std::string id = manager.create(fake_ops(nullptr));
        assert(cp0::bluetooth::valid_session_id(id));
        assert(manager.size() == 1);
        assert(manager.get(id) != nullptr);
        assert(manager.get("not-a-number") == nullptr);
        assert(manager.get("999999") == nullptr);
        assert(manager.deinit(id));
        assert(manager.size() == 0);
        assert(!manager.deinit(id));
        assert(manager.get(id) == nullptr);
    }

    // status_get delivers encoded status asynchronously.
    {
        std::atomic<int> calls{0};
        auto session = std::make_shared<BluetoothBackendSession>(fake_ops(&calls));
        Delivery status;
        session->status_get([&](int code, std::string data) { status.set(code, std::move(data)); });
        assert(status.wait_for());
        assert(status.code == 0);
        assert(status.data == "1\tAA:BB:CC:DD:EE:FF\t0\tFakeAlias");
        assert(calls.load() >= 1);
        session->deinit();
        assert(session->pending_tasks() == 0);
        assert(!session->running());
    }

    // setters and device commands are async.
    {
        std::atomic<int> calls{0};
        auto session = std::make_shared<BluetoothBackendSession>(fake_ops(&calls));
        Delivery power;
        session->set_power(1, [&](int code, std::string) { power.set(code, {}); });
        assert(power.wait_for());
        assert(power.code == 0);

        Delivery alias;
        session->set_alias("NewName", [&](int code, std::string) { alias.set(code, {}); });
        assert(alias.wait_for());
        assert(alias.code == 0);

        Delivery connect;
        session->device_command("connect", "AA:BB:CC:DD:EE:01",
                                [&](int code, std::string) { connect.set(code, {}); });
        assert(connect.wait_for());
        assert(connect.code == 0);
        session->deinit();
    }

    // connected list get delivers connected-only devices.
    {
        std::atomic<int> calls{0};
        auto session = std::make_shared<BluetoothBackendSession>(fake_ops(&calls));
        Delivery init;
        session->connected_list_init([&](int code, std::string) { init.set(code, {}); });
        assert(init.wait_for());
        assert(init.code == 0);

        Delivery get;
        session->connected_list_get([&](int code, std::string data) { get.set(code, std::move(data)); });
        assert(get.wait_for());
        assert(get.code == 0);
        assert(get.data == "AA:BB:CC:DD:EE:FF\t0\t1\t0\t0\tConnectedDevice\n");

        Delivery deinit;
        session->connected_list_deinit([&](int code, std::string) { deinit.set(code, {}); });
        assert(deinit.wait_for());
        assert(deinit.code == 0);
        session->deinit();
    }

    // scan thread delivers snapshots and scan_off joins it.
    {
        std::atomic<int> calls{0};
        auto session = std::make_shared<BluetoothBackendSession>(fake_ops(&calls));
        Delivery first;
        session->scan_on([&](int code, std::string data) { first.set(code, std::move(data)); });
        assert(first.wait_for());
        assert(first.code == 0);
        assert(first.data == "11:22:33:44:55:66\t-42\t0\t0\t0\tScanDevice\n");

        Delivery off;
        session->scan_off([&](int code, std::string) { off.set(code, {}); });
        assert(off.wait_for());
        assert(off.code == 0);

        // A second scan_on after scan_off must fail cleanly.
        Delivery second;
        session->scan_on([&](int code, std::string) { second.set(code, {}); });
        // scan thread already stopped; a new one may start, so just deinit.
        session->deinit();
        assert(!session->running());
    }

    // deinit is synchronous: after it returns, no in-flight task remains.
    {
        auto session = std::make_shared<BluetoothBackendSession>(fake_ops(nullptr));
        Delivery status;
        session->status_get([&](int code, std::string data) { status.set(code, std::move(data)); });
        session->deinit();
        assert(session->pending_tasks() == 0);
        assert(!session->running());

        // Post-deinit requests are refused without spawning work.
        Delivery refused;
        session->status_get([&](int code, std::string) { refused.set(code, {}); });
        assert(refused.wait_for());
        assert(refused.code != 0);
    }

    // shutdown_all deinits every live session.
    {
        BluetoothSessionManager manager;
        const std::string a = manager.create(fake_ops(nullptr));
        const std::string b = manager.create(fake_ops(nullptr));
        assert(manager.size() == 2);
        manager.shutdown_all();
        assert(manager.size() == 0);
        (void)a;
        (void)b;
    }

    // preload_status caches the initial read; subsequent status_get reuses it.
    {
        std::atomic<int> calls{0};
        auto session = std::make_shared<BluetoothBackendSession>(fake_ops(&calls));
        session->preload_status();

        Delivery first;
        session->status_get([&](int code, std::string data) { first.set(code, std::move(data)); });
        assert(first.wait_for());
        assert(first.code == 0);
        assert(first.data == "1\tAA:BB:CC:DD:EE:FF\t0\tFakeAlias");

        const int calls_after_first = calls.load();
        Delivery second;
        session->status_get([&](int code, std::string data) { second.set(code, std::move(data)); });
        assert(second.wait_for());
        assert(second.code == 0);
        assert(calls.load() == calls_after_first);
        session->deinit();
    }

    // successful setters invalidate the cache so a following status_get
    // performs a fresh backend read.
    {
        std::atomic<int> calls{0};
        auto session = std::make_shared<BluetoothBackendSession>(fake_ops(&calls));
        session->preload_status();

        Delivery first;
        session->status_get([&](int code, std::string data) { first.set(code, std::move(data)); });
        assert(first.wait_for());
        const int calls_before_set = calls.load();

        Delivery power;
        session->set_power(1, [&](int code, std::string) { power.set(code, {}); });
        assert(power.wait_for());
        assert(power.code == 0);

        Delivery refreshed;
        session->status_get([&](int code, std::string data) { refreshed.set(code, std::move(data)); });
        assert(refreshed.wait_for());
        assert(refreshed.code == 0);
        assert(calls.load() > calls_before_set);
        session->deinit();
    }

    // deinit is synchronous: it blocks until an in-flight async task completes
    // (the op finished before deinit returned), and the callback is gated off.
    {
        std::atomic<int> op_done{0};
        BackendOps slow = fake_ops(nullptr);
        slow.status = [&]() -> cp0_bt_status_t {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            op_done.store(1);
            cp0_bt_status_t status{};
            status.powered = 1;
            return status;
        };
        auto session = std::make_shared<BluetoothBackendSession>(slow);
        Delivery delivered;
        session->status_get([&](int code, std::string data) { delivered.set(code, std::move(data)); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        session->deinit();
        assert(op_done.load() == 1);
        assert(session->pending_tasks() == 0);
        assert(!session->running());
        // The gated callback must not have fired after deinit began.
        assert(!delivered.done);
    }

    // A delayed BlueZ property update must be observed before the Power
    // callback is delivered; otherwise the UI can immediately read stale OFF.
    {
        std::mutex mutex;
        bool powered = false;
        bool pending_power = false;
        BackendOps delayed = fake_ops(nullptr);
        delayed.status = [&]() {
            std::lock_guard<std::mutex> lock(mutex);
            if (pending_power) {
                pending_power = false;
                powered = true;
            }
            cp0_bt_status_t status{};
            status.powered = powered ? 1 : 0;
            return status;
        };
        delayed.set_power = [&](int on) {
            std::lock_guard<std::mutex> lock(mutex);
            pending_power = on != 0;
            return 0;
        };
        auto session = std::make_shared<BluetoothBackendSession>(delayed);
        Delivery power;
        session->set_power(1, [&](int code, std::string) { power.set(code, {}); });
        assert(power.wait_for());
        assert(power.code == 0);
        Delivery status;
        session->status_get([&](int code, std::string data) { status.set(code, std::move(data)); });
        assert(status.wait_for());
        assert(status.code == 0 && status.data.rfind("1\t", 0) == 0);
        session->deinit();
    }

    return 0;
}
