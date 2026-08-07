#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace cp0::audio {

class QuietPeriodGate
{
public:
    void request()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (++generation_ == 0)
                ++generation_;
        }
        cv_.notify_one();
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_one();
    }

    template <class Rep, class Period>
    std::uint64_t wait_until_quiet(
        const std::chrono::duration<Rep, Period> &quiet_period)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {
            return stopped_ || generation_ != observed_generation_;
        });

        while (!stopped_) {
            observed_generation_ = generation_;
            if (!cv_.wait_for(lock, quiet_period, [this] {
                    return stopped_ || generation_ != observed_generation_;
                }))
                return observed_generation_;
        }
        return 0;
    }

    bool current(std::uint64_t generation) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return !stopped_ && generation != 0 && generation == generation_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::uint64_t generation_ = 0;
    std::uint64_t observed_generation_ = 0;
    bool stopped_ = false;
};

} // namespace cp0::audio
