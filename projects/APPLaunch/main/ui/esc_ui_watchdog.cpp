#include "esc_ui_watchdog.h"

#include "cp0_lvgl_app.h"
#include "cp0_esc_state.h"
#include "keyboard_input.h"

#include <chrono>
#include <cstdlib>

namespace {
constexpr auto kPollInterval = std::chrono::milliseconds(50);
constexpr int kUiWatchdogExitCode = 75;
}

EscUiWatchdog::~EscUiWatchdog()
{
    shutdown();
}

void EscUiWatchdog::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) return;
    stopping_ = false;
    started_ = true;
    thread_ = std::thread(&EscUiWatchdog::run, this);
}

void EscUiWatchdog::arm()
{
    std::lock_guard<std::mutex> lock(mutex_);
    armed_ = true;
    wake_.notify_all();
}

void EscUiWatchdog::disarm()
{
    std::lock_guard<std::mutex> lock(mutex_);
    armed_ = false;
    wake_.notify_all();
}

void EscUiWatchdog::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) return;
        stopping_ = true;
        armed_ = false;
        wake_.notify_all();
    }
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = false;
}

void EscUiWatchdog::run()
{
    using clock = std::chrono::steady_clock;
    auto pressed_at = clock::time_point{};
    bool tracking = false;

    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        wake_.wait_for(lock, kPollInterval);
        if (stopping_) break;
        const bool armed = armed_;
        lock.unlock();

        const bool esc_down = cp0_esc_state_read() != 0;
        const auto now = clock::now();
        if (!armed || !esc_down) {
            tracking = false;
        } else if (!tracking) {
            tracking = true;
            pressed_at = now;
        } else {
            const auto held_ms = static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - pressed_at).count());
            if (esc_ui_watchdog_should_recover(armed, esc_down, held_ms))
                std::_Exit(kUiWatchdogExitCode);
        }

        lock.lock();
    }
}
