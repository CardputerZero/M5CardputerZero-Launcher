#pragma once

#include "cp0_lvgl_app.h"

#include <chrono>
#include <mutex>

namespace cp0::battery {

class SnapshotCache {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kStaleAfter = std::chrono::seconds(60);

    void update(const cp0_battery_info_t &info,
                Clock::time_point now = Clock::now())
    {
        if (info.valid != 1) return;
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = info;
        last_update_ = now;
        has_snapshot_ = true;
    }

    cp0_battery_info_t read(Clock::time_point now = Clock::now()) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_snapshot_ || now - last_update_ >= kStaleAfter)
            return {};
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    cp0_battery_info_t snapshot_{};
    Clock::time_point last_update_{};
    bool has_snapshot_ = false;
};

} // namespace cp0::battery
