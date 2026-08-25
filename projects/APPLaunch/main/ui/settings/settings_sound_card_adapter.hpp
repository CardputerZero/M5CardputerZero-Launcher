#pragma once

#include "cp0_bounded_task_registry.hpp"
#include "hal_lvgl_bsp.h"
#include "settings_sound_card_model.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace ui_test_soundcard {

class SoundCardApiAdapter {
public:
    using Completion = std::function<void(int, std::string)>;
    using Invoker = std::function<void(std::list<std::string>, Completion)>;

    struct Result {
        uint64_t generation = 0;
        Completion handler;
        int code = -1;
        std::string data;
    };

    SoundCardApiAdapter()
        : invoker_([](std::list<std::string> arguments, Completion callback) {
              cp0_signal_soundcard_api(std::move(arguments), std::move(callback));
          })
    {
    }

    explicit SoundCardApiAdapter(
        Invoker invoker,
        std::chrono::milliseconds timeout = std::chrono::seconds(3))
        : invoker_(std::move(invoker)), timeout_(timeout)
    {
    }

    SoundCardApiAdapter(const SoundCardApiAdapter &) = delete;
    SoundCardApiAdapter &operator=(const SoundCardApiAdapter &) = delete;

    ~SoundCardApiAdapter()
    {
        shutdown();
    }

    bool request(std::list<std::string> arguments,
                 uint64_t generation,
                 Completion handler)
    {
        if (!handler) return false;
        const auto dispatch = dispatch_;
        if (!invoker_) {
            enqueue(dispatch,
                    Result{generation, std::move(handler), -1,
                           "Soundcard backend unavailable"});
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(dispatch->mutex);
            if (dispatch->stopped) return false;
        }

        auto completion = std::make_shared<CompletionState>();
        auto callback = [dispatch, generation, handler = std::move(handler), completion](
                            int code, std::string data) mutable {
            if (completion->delivered.exchange(true, std::memory_order_acq_rel)) return;
            enqueue(dispatch, Result{generation, std::move(handler), code, std::move(data)});
            completion->condition.notify_all();
        };
        const Completion failure_callback = callback;
        const Invoker invoker = invoker_;
        if (tasks_.start([invoker,
                          arguments = std::move(arguments),
                          callback = std::move(callback),
                          completion,
                          timeout = timeout_]() mutable {
                try {
                    invoker(std::move(arguments), callback);
                } catch (...) {
                    callback(-1, "Soundcard service unavailable");
                    return;
                }
                std::unique_lock<std::mutex> lock(completion->mutex);
                if (!completion->delivered.load(std::memory_order_acquire) &&
                    !completion->condition.wait_for(
                        lock,
                        timeout,
                        [completion] {
                            return completion->delivered.load(std::memory_order_acquire);
                        })) {
                    lock.unlock();
                    callback(-1, "Soundcard request timed out");
                }
            }))
            return true;

        failure_callback(-1, "Soundcard request could not be scheduled");
        return false;
    }

    template <typename Consumer>
    void drain(Consumer &&consumer)
    {
        std::deque<Result> pending;
        {
            std::lock_guard<std::mutex> lock(dispatch_->mutex);
            pending.swap(dispatch_->pending);
        }
        for (auto &result : pending) {
            try {
                consumer(result);
            } catch (...) {
            }
        }
        tasks_.reap_finished();
    }

    void discard_pending()
    {
        std::lock_guard<std::mutex> lock(dispatch_->mutex);
        dispatch_->pending.clear();
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(dispatch_->mutex);
            dispatch_->stopped = true;
            dispatch_->pending.clear();
        }
        tasks_.join_all();
    }

private:
    struct CompletionState {
        std::atomic<bool> delivered{false};
        std::mutex mutex;
        std::condition_variable condition;
    };

    struct DispatchState {
        std::mutex mutex;
        bool stopped = false;
        std::deque<Result> pending;
    };

    static void enqueue(const std::shared_ptr<DispatchState> &dispatch, Result result) noexcept
    {
        if (!dispatch) return;
        try {
            std::lock_guard<std::mutex> lock(dispatch->mutex);
            if (dispatch->stopped) return;
            dispatch->pending.emplace_back(std::move(result));
        } catch (...) {
        }
    }

    Invoker invoker_;
    std::chrono::milliseconds timeout_ = std::chrono::seconds(3);
    Cp0BoundedTaskRegistry tasks_;
    std::shared_ptr<DispatchState> dispatch_ = std::make_shared<DispatchState>();
};

}
