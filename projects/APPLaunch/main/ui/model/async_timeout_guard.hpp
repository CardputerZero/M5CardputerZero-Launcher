#pragma once

#include <cstdint>
#include <functional>
#include <utility>

// Bounds an asynchronous operation with a timeout. The LVGL adapter injects
// timer creation/destruction; tests inject recording hooks so the logic is
// fully host-testable without LVGL.
//
// A generation counter makes stale timer firings harmless: once complete() or
// cancel() runs, an in-flight timer callback becomes a no-op.
class AsyncTimeoutGuard
{
public:
    AsyncTimeoutGuard() = default;
    ~AsyncTimeoutGuard() { cancel(); }

    AsyncTimeoutGuard(const AsyncTimeoutGuard &) = delete;
    AsyncTimeoutGuard &operator=(const AsyncTimeoutGuard &) = delete;

    // Starts the guard. If complete() is not called within timeout_ms, the
    // timeout hook fires exactly once.
    void begin(uint32_t timeout_ms, std::function<void()> on_timeout)
    {
        cancel();
        pending_ = true;
        ++generation_;
        on_timeout_ = std::move(on_timeout);
        const uint64_t generation = generation_;
        if (start_)
            start_(timeout_ms, [this, generation] { timer_fired(generation); });
    }

    // Marks success. Returns true if the operation was still pending (i.e. it
    // completed before timing out).
    bool complete()
    {
        if (!pending_)
            return false;
        pending_ = false;
        ++generation_;
        on_timeout_ = {};
        if (stop_)
            stop_();
        return true;
    }

    // Cancels without running the timeout hook.
    void cancel()
    {
        if (!pending_)
            return;
        pending_ = false;
        ++generation_;
        on_timeout_ = {};
        if (stop_)
            stop_();
    }

    bool pending() const { return pending_; }

    // Timer control hooks. The LVGL adapter passes lv_timer_create/lv_timer_delete;
    // tests pass recording lambdas.
    void set_timer_hooks(std::function<void(uint32_t, std::function<void()>)> start,
                         std::function<void()> stop)
    {
        start_ = std::move(start);
        stop_ = std::move(stop);
    }

private:
    void timer_fired(uint64_t generation)
    {
        if (!pending_ || generation != generation_)
            return;
        pending_ = false;
        ++generation_;
        std::function<void()> on_timeout = std::move(on_timeout_);
        on_timeout_ = {};
        if (stop_)
            stop_();
        if (on_timeout)
            on_timeout();
    }

    bool pending_ = false;
    std::function<void()> on_timeout_;
    std::function<void(uint32_t, std::function<void()>)> start_;
    std::function<void()> stop_;
    uint64_t generation_ = 0;
};
