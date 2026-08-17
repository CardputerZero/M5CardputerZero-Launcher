#pragma once

#include "cp0_bluetooth_api_contract.hpp"
#include "cp0_lvgl_app.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cp0::bluetooth {

using BtCallback = std::function<void(int, std::string)>;

// Platform Bluetooth operations. Injected so the session lifecycle (threads,
// futures, and synchronous deinit semantics) can be unit-tested without a
// real BlueZ / D-Bus stack. The hardware backend wraps cp0_bluetooth_backend,
// the SDL backend wraps hal_bt_*.
struct BackendOps {
    std::function<cp0_bt_status_t()> status;
    std::function<int(int)> set_power;
    std::function<int(const char *)> set_alias;
    std::function<int(int)> set_discoverable;
    std::function<int()> start_discovery;
    std::function<int()> stop_discovery;
    std::function<int(cp0_bt_device_t *, int, bool)> list; // (out, max, connected_only)
    std::function<int(const char *)> pair;
    std::function<int(const char *)> connect;
    std::function<int(const char *)> disconnect;
    std::function<int(const char *)> remove;
};

// Owns the per-session backend work. Callbacks fire on backend worker threads
// (never on the LVGL main thread); the UI layer marshals them to the main
// thread. deinit() is synchronous: after it returns no worker thread is
// running and no further callback can fire.
class BluetoothBackendSession
{
public:
    explicit BluetoothBackendSession(BackendOps ops);
    ~BluetoothBackendSession();

    BluetoothBackendSession(const BluetoothBackendSession &) = delete;
    BluetoothBackendSession &operator=(const BluetoothBackendSession &) = delete;
    BluetoothBackendSession(BluetoothBackendSession &&) = delete;
    BluetoothBackendSession &operator=(BluetoothBackendSession &&) = delete;

    // Idempotent synchronous shutdown: signals workers, joins the scan thread,
    // and waits for all in-flight async tasks. After deinit() returns no worker
    // thread is running and no further callback can fire.
    void deinit();

    // Starts an asynchronous preload of the adapter status. The result is
    // cached inside the session; status_get() then returns the cached value
    // instead of hitting BlueZ again. Callbacks are never invoked from this
    // method; preload completion is observed through status_get().
    void preload_status();

    // Async status read. Returns the cached status if available, otherwise
    // waits a bounded time for preload_status() and finally falls back to a
    // direct backend read for legacy callers. Callback receives
    // (0, encoded status) or (code != 0).
    void status_get(BtCallback callback);

    void set_power(int on, BtCallback callback);
    void set_alias(const std::string &alias, BtCallback callback);
    void set_discoverable(int on, BtCallback callback);

    // "pair" | "connect" | "disconnect" | "remove" on a device address.
    void device_command(const std::string &command, const std::string &address,
                        BtCallback callback);

    // Scan sub-page lifecycle. scan_on starts a periodic scan thread that
    // delivers each device snapshot to the callback; scan_off stops and joins
    // it before invoking the callback.
    void scan_on(BtCallback callback);
    void scan_off(BtCallback callback);

    // Connected sub-page lifecycle. init is a synchronous no-op marker, get
    // reads the connected-only list asynchronously, deinit is a synchronous
    // no-op marker.
    void connected_list_init(BtCallback callback);
    void connected_list_get(BtCallback callback);
    void connected_list_deinit(BtCallback callback);

    bool running() const { return running_.load(); }
    int pending_tasks() const { return pending_tasks_.load(); }

private:
    void run_async(std::function<Reply()> operation, BtCallback callback);
    void invalidate_status();
    void scan_loop();
    std::thread detach_scan_thread();
    static void deliver(BtCallback callback, int code, std::string data);

    BackendOps ops_;
    std::atomic<bool> running_{true};
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> scan_running_{false};
    std::atomic<int> pending_tasks_{0};

    std::mutex mutex_;
    std::condition_variable scan_cv_;
    std::condition_variable done_cv_;

    std::mutex status_mutex_;
    // Serializes adapter state reads and writes so an in-flight initial
    // preload cannot publish an old snapshot after BtPower changes state.
    std::mutex state_operation_mutex_;
    std::condition_variable status_cv_;
    cp0_bt_status_t status_cache_{};
    bool status_ready_ = false;
    bool preload_started_ = false;
    bool preload_inflight_ = false;

    std::thread scan_thread_;
    BtCallback scan_callback_;
    std::vector<std::future<void>> futures_;
};

// Process-wide registry keyed by numeric session id (decimal string). deinit
// is synchronous and removes the session.
class BluetoothSessionManager
{
public:
    // Creates a session, returns its id.
    std::string create(BackendOps ops);
    // Synchronously deinits and removes the session. Returns false if unknown.
    bool deinit(const std::string &session_id);
    // Returns the session, or null if unknown. Callers must not hold the
    // result across deinit of the same id.
    std::shared_ptr<BluetoothBackendSession> get(const std::string &session_id);
    // Synchronously deinits every session (process shutdown path).
    void shutdown_all();
    std::size_t size();

private:
    static bool parse_id(const std::string &session_id, uint64_t &out);

    std::mutex mutex_;
    uint64_t next_id_ = 1;
    std::map<uint64_t, std::shared_ptr<BluetoothBackendSession>> sessions_;
};

} // namespace cp0::bluetooth
