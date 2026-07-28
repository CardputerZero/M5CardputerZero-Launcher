#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

inline constexpr std::uint32_t ESC_UI_RECOVERY_DELAY_MS = 3500;

constexpr bool esc_ui_watchdog_should_recover(bool armed, bool esc_down,
                                               std::uint32_t held_ms) noexcept
{
    return armed && esc_down && held_ms >= ESC_UI_RECOVERY_DELAY_MS;
}

class EscUiWatchdog
{
public:
    EscUiWatchdog() = default;
    ~EscUiWatchdog();

    EscUiWatchdog(const EscUiWatchdog &) = delete;
    EscUiWatchdog &operator=(const EscUiWatchdog &) = delete;

    void start();
    void arm();
    void disarm();
    void shutdown();

private:
    void run();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    bool started_ = false;
    bool armed_ = false;
    bool stopping_ = false;
};
