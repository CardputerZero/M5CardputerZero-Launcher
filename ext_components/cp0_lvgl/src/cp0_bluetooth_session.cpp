#include "cp0_bluetooth_session.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

namespace cp0::bluetooth {
namespace {

constexpr std::chrono::milliseconds kScanPeriodMs{2500};

} // namespace

BluetoothBackendSession::BluetoothBackendSession(BackendOps ops)
    : ops_(std::move(ops))
{
}

BluetoothBackendSession::~BluetoothBackendSession()
{
    deinit();
}

void BluetoothBackendSession::deinit()
{
    if (shutdown_.exchange(true))
        return;
    running_.store(false);
    scan_running_.store(false);
    scan_cv_.notify_all();
    status_cv_.notify_all(); // wake status_get waiters so they exit promptly

    std::thread scan = detach_scan_thread();
    if (scan.joinable())
        scan.join();

    // C++ std::async tasks cannot be force-killed. The shutdown_/running_
    // flags suppress their callbacks, so we wait for the tasks themselves to
    // finish. Every backend operation must be bounded (the hardware BlueZ
    // client uses a finite D-Bus timeout) so this wait cannot hang forever.
    std::unique_lock<std::mutex> lock(mutex_);
    done_cv_.wait(lock, [this] { return pending_tasks_.load() == 0; });
    futures_.clear();
}

void BluetoothBackendSession::run_async(std::function<Reply()> operation, BtCallback callback)
{
    if (shutdown_.load()) {
        deliver(callback, -1, "session stopped");
        return;
    }

    pending_tasks_.fetch_add(1);
    if (shutdown_.load()) {
        pending_tasks_.fetch_sub(1);
        done_cv_.notify_all();
        deliver(callback, -1, "session stopped");
        return;
    }

    auto shared_callback = std::make_shared<BtCallback>(std::move(callback));
    try {
        std::future<void> future = std::async(std::launch::async,
            [this, operation = std::move(operation), shared_callback]() mutable {
                struct PendingGuard {
                    std::atomic<int> &pending;
                    std::condition_variable &done;
                    ~PendingGuard()
                    {
                        pending.fetch_sub(1);
                        done.notify_all();
                    }
                } guard{pending_tasks_, done_cv_};

                Reply reply;
                try {
                    reply = operation();
                } catch (...) {
                    reply = {-1, "bluetooth backend failure"};
                }
                if (running_.load())
                    deliver(*shared_callback, reply.code, reply.data);
            });
        std::lock_guard<std::mutex> lock(mutex_);
        futures_.push_back(std::move(future));
    } catch (...) {
        pending_tasks_.fetch_sub(1);
        done_cv_.notify_all();
        deliver(*shared_callback, -1, "unable to start backend task");
    }
}

void BluetoothBackendSession::preload_status()
{
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        if (preload_started_ || shutdown_.load())
            return;
        preload_started_ = true;
        preload_inflight_ = true;
    }

    // Run the initial query in the background. The result is stored in the
    // session cache and observed later through status_get(); the callback is
    // intentionally empty because this operation itself is fire-and-forget.
    run_async([this]() -> Reply {
        cp0_bt_status_t status{};
        try {
            status = ops_.status();
        } catch (...) {
            std::lock_guard<std::mutex> lock(status_mutex_);
            preload_inflight_ = false;
            status_cv_.notify_all();
            return {-1, "bluetooth backend failure"};
        }
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            status_cache_ = status;
            status_ready_ = true;
            preload_inflight_ = false;
        }
        status_cv_.notify_all();
        return {0, encode_status(status)};
    }, [this](int code, std::string) {
        // run_async may fail before starting a worker (e.g. std::async throws).
        // In that case clear preload_inflight_ so status_get falls back to a
        // direct backend read instead of waiting for a task that never runs.
        if (code != 0) {
            std::lock_guard<std::mutex> lock(status_mutex_);
            preload_inflight_ = false;
            status_cv_.notify_all();
        }
    });
}

void BluetoothBackendSession::status_get(BtCallback callback)
{
    run_async([this]() -> Reply {
        std::unique_lock<std::mutex> lock(status_mutex_);
        if (status_ready_)
            return {0, encode_status(status_cache_)};

        if (preload_inflight_) {
            // Initial preload is still running. Wait a bounded time so the UI
            // keeps its own 3-second timeout meaningful. Wake up immediately
            // when the session is deinitialized so deinit() does not wait the
            // full 2.5s.
            if (status_cv_.wait_for(lock, std::chrono::milliseconds(2000),
                                    [this] { return status_ready_ || !running_.load(); })) {
                if (status_ready_)
                    return {0, encode_status(status_cache_)};
                return {-2, "session stopped"};
            }
            return {-2, "bluetooth status read timeout"};
        }

        // Either preload never started (legacy session use) or the cache was
        // invalidated by a successful setter (power/alias/discoverable). Read
        // the adapter directly so the UI receives a fresh value.
        lock.unlock();
        cp0_bt_status_t status{};
        try {
            status = ops_.status();
        } catch (...) {
            return {-1, "bluetooth backend failure"};
        }
        return {0, encode_status(status)};
    }, std::move(callback));
}

void BluetoothBackendSession::invalidate_status()
{
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_ready_ = false;
}

void BluetoothBackendSession::set_power(int on, BtCallback callback)
{
    run_async([this, on]() -> Reply {
        const int result = ops_.set_power(on);
        if (result == 0)
            invalidate_status();
        return {result, {}};
    }, std::move(callback));
}

void BluetoothBackendSession::set_alias(const std::string &alias, BtCallback callback)
{
    run_async([this, alias]() -> Reply {
        const int result = ops_.set_alias(alias.c_str());
        if (result == 0)
            invalidate_status();
        return {result, {}};
    }, std::move(callback));
}

void BluetoothBackendSession::set_discoverable(int on, BtCallback callback)
{
    run_async([this, on]() -> Reply {
        const int result = ops_.set_discoverable(on);
        if (result == 0)
            invalidate_status();
        return {result, {}};
    }, std::move(callback));
}

void BluetoothBackendSession::device_command(const std::string &command,
                                             const std::string &address,
                                             BtCallback callback)
{
    run_async([this, command, address]() -> Reply {
        int result = -1;
        if (command == "pair")
            result = ops_.pair(address.c_str());
        else if (command == "connect")
            result = ops_.connect(address.c_str());
        else if (command == "disconnect")
            result = ops_.disconnect(address.c_str());
        else if (command == "remove")
            result = ops_.remove(address.c_str());
        return {result, {}};
    }, std::move(callback));
}

void BluetoothBackendSession::scan_on(BtCallback callback)
{
    bool started = false;
    bool unavailable = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load() || shutdown_.load() || scan_thread_.joinable()) {
            unavailable = true;
        } else {
            scan_callback_ = callback;
            scan_running_.store(true);
            try {
                scan_thread_ = std::thread([this] { scan_loop(); });
                started = true;
            } catch (...) {
                scan_callback_ = {};
                scan_running_.store(false);
                scan_thread_ = {};
            }
        }
    }
    if (started)
        return;
    deliver(callback, -1, unavailable ? "scan unavailable" : "unable to start scan thread");
}

void BluetoothBackendSession::scan_off(BtCallback callback)
{
    std::thread scan;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scan_running_.store(false);
        scan_callback_ = {};
        scan = std::move(scan_thread_);
    }
    scan_cv_.notify_all();
    if (scan.joinable())
        scan.join();
    deliver(callback, 0, {});
}

void BluetoothBackendSession::scan_loop()
{
    const bool started = ops_.start_discovery() == 0;
    for (;;) {
        BtCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_.load() || !scan_running_.load())
                break;
            callback = scan_callback_;
        }
        if (callback) {
            std::vector<cp0_bt_device_t> devices(static_cast<size_t>(CP0_BT_DEVICE_MAX));
            int count = ops_.list(devices.data(), CP0_BT_DEVICE_MAX, false);
            if (count < 0)
                count = 0;
            deliver(callback, 0, encode_devices(devices.data(), count));
        }
        std::unique_lock<std::mutex> lock(mutex_);
        if (scan_cv_.wait_for(lock, kScanPeriodMs,
                              [this] { return !running_.load() || !scan_running_.load(); }))
            break;
    }
    if (started)
        ops_.stop_discovery();
}

std::thread BluetoothBackendSession::detach_scan_thread()
{
    std::lock_guard<std::mutex> lock(mutex_);
    scan_callback_ = {};
    return std::move(scan_thread_);
}

void BluetoothBackendSession::connected_list_init(BtCallback callback)
{
    // BlueZ needs no per-sub-page initialization; keep the lifecycle marker so
    // the UI state machine can share one code path for init/get/deinit.
    deliver(callback, 0, {});
}

void BluetoothBackendSession::connected_list_get(BtCallback callback)
{
    run_async([this]() -> Reply {
        std::vector<cp0_bt_device_t> devices(static_cast<size_t>(CP0_BT_DEVICE_MAX));
        int count = ops_.list(devices.data(), CP0_BT_DEVICE_MAX, true);
        if (count < 0)
            count = 0;
        return {0, encode_devices(devices.data(), count)};
    }, std::move(callback));
}

void BluetoothBackendSession::connected_list_deinit(BtCallback callback)
{
    deliver(callback, 0, {});
}

void BluetoothBackendSession::deliver(BtCallback callback, int code, std::string data)
{
    invoke_callback(callback, code, data);
}

std::string BluetoothSessionManager::create(BackendOps ops)
{
    uint64_t id = 0;
    std::shared_ptr<BluetoothBackendSession> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = next_id_++;
        session = std::make_shared<BluetoothBackendSession>(std::move(ops));
        sessions_.emplace(id, session);
    }
    // cp0_signal_bt_api("BtSessionInit") starts the initial async status
    // preload immediately after creating the session handle.
    session->preload_status();
    return std::to_string(id);
}

bool BluetoothSessionManager::deinit(const std::string &session_id)
{
    uint64_t id = 0;
    if (!parse_id(session_id, id))
        return false;

    std::shared_ptr<BluetoothBackendSession> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = sessions_.find(id);
        if (found == sessions_.end())
            return false;
        session = found->second;
        sessions_.erase(found);
    }
    session->deinit();
    return true;
}

std::shared_ptr<BluetoothBackendSession> BluetoothSessionManager::get(const std::string &session_id)
{
    uint64_t id = 0;
    if (!parse_id(session_id, id))
        return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(id);
    return found == sessions_.end() ? nullptr : found->second;
}

void BluetoothSessionManager::shutdown_all()
{
    std::vector<std::shared_ptr<BluetoothBackendSession>> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &entry : sessions_)
            sessions.push_back(entry.second);
        sessions_.clear();
    }
    for (auto &session : sessions)
        session->deinit();
}

std::size_t BluetoothSessionManager::size()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

bool BluetoothSessionManager::parse_id(const std::string &session_id, uint64_t &out)
{
    if (!valid_session_id(session_id))
        return false;
    try {
        out = std::stoull(session_id);
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace cp0::bluetooth
