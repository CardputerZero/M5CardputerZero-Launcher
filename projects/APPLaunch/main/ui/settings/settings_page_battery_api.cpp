#include "settings_battery_api.hpp"

#include "hal_lvgl_bsp.h"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace {

struct Invocation {
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    bool cancelled = false;
    int code = -1;
    std::string payload;
};

void complete_invocation(const std::shared_ptr<Invocation> &invocation,
                         int code,
                         std::string payload)
{
    if (!invocation) return;
    {
        std::lock_guard<std::mutex> lock(invocation->mutex);
        if (invocation->completed) return;
        invocation->completed = true;
        invocation->code = code;
        invocation->payload = std::move(payload);
    }
    invocation->condition.notify_all();
}

} // namespace

SettingsBatteryApi::SettingsBatteryApi()
    : dispatch_([](Arguments arguments, Callback callback) {
          cp0_signal_bq27220_api(std::move(arguments), std::move(callback));
      })
{
}

SettingsBatteryApi::SettingsBatteryApi(Dispatch dispatch)
    : dispatch_(std::move(dispatch))
{
    if (!dispatch_) {
        dispatch_ = [](Arguments arguments, Callback callback) {
            cp0_signal_bq27220_api(std::move(arguments), std::move(callback));
        };
    }
}

void SettingsBatteryApi::read(Callback callback) const
{
    request({"Read"}, std::move(callback));
}

void SettingsBatteryApi::calibrate(int command_index, Callback callback) const
{
    if (!valid_calibration_index(command_index)) {
        if (callback) callback(-1, "invalid calibration index");
        return;
    }
    request(calibration_arguments(command_index), std::move(callback));
}

bool SettingsBatteryApi::valid_calibration_index(int command_index)
{
    return command_index >= 0 && command_index <= 3;
}

SettingsBatteryApi::Arguments SettingsBatteryApi::calibration_arguments(int command_index)
{
    if (!valid_calibration_index(command_index)) return {};
    return {"Calibrate", std::to_string(command_index)};
}

void SettingsBatteryApi::request(Arguments arguments, Callback callback) const
{
    if (!callback) return;
    if (!dispatch_) {
        callback(-1, "battery api unavailable");
        return;
    }

    Callback fallback = callback;
    try {
        dispatch_(std::move(arguments), std::move(callback));
    } catch (...) {
        fallback(-1, "battery api exception");
    }
}

struct SettingsBatteryRequestCoordinator::Impl
    : std::enable_shared_from_this<SettingsBatteryRequestCoordinator::Impl> {
    using Clock = std::chrono::steady_clock;

    SettingsBatteryApi api;
    Post post;
    std::chrono::milliseconds timeout;
    mutable std::mutex mutex;
    std::mutex lifecycle_mutex;
    std::thread worker;
    std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);
    std::shared_ptr<Invocation> active_invocation;
    std::uint64_t next_generation = 0;
    std::uint64_t active_generation = 0;
    bool pending = false;
    bool shutting_down = false;

    Impl(SettingsBatteryApi request_api, Post request_post, std::chrono::milliseconds request_timeout)
        : api(std::move(request_api)),
          post(std::move(request_post)),
          timeout(request_timeout)
    {
    }

    bool start(SettingsBatteryOperation operation,
               int calibration_index,
               Completion completion)
    {
        if (!completion || !post) return false;
        if (operation == SettingsBatteryOperation::Calibrate &&
            !SettingsBatteryApi::valid_calibration_index(calibration_index))
            return false;

        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
        std::thread previous;
        std::shared_ptr<Invocation> invocation = std::make_shared<Invocation>();
        std::uint64_t request_generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shutting_down || pending) return false;
            previous = std::move(worker);
            pending = true;
            request_generation = ++next_generation;
            active_generation = request_generation;
            active_invocation = invocation;
        }

        if (previous.joinable()) previous.join();

        try {
            const auto self = shared_from_this();
            worker = std::thread(
                [self, operation, calibration_index, request_generation,
                 invocation, completion = std::move(completion)]() mutable {
                    self->run(operation,
                              calibration_index,
                              request_generation,
                              invocation,
                              std::move(completion));
                });
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            if (active_generation == request_generation) {
                pending = false;
                active_invocation.reset();
            }
            return false;
        }
        return true;
    }

    void run(SettingsBatteryOperation operation,
             int calibration_index,
             std::uint64_t request_generation,
             const std::shared_ptr<Invocation> &invocation,
             Completion completion)
    {
        auto callback = [invocation](int code, std::string payload) {
            complete_invocation(invocation, code, std::move(payload));
        };

        bool cancelled_before_request = false;
        {
            std::lock_guard<std::mutex> lock(invocation->mutex);
            cancelled_before_request = invocation->cancelled;
        }

        if (!cancelled_before_request) {
            try {
                if (operation == SettingsBatteryOperation::Read)
                    api.read(std::move(callback));
                else
                    api.calibrate(calibration_index, std::move(callback));
            } catch (...) {
                complete_invocation(invocation, -1, "battery api exception");
            }
        }

        SettingsBatteryOperationResult result;
        result.operation = operation;
        result.generation = request_generation;
        result.calibration_index = calibration_index;

        std::unique_lock<std::mutex> invocation_lock(invocation->mutex);
        const bool completed = invocation->condition.wait_for(
            invocation_lock,
            timeout,
            [&] { return invocation->completed || invocation->cancelled; });
        if (invocation->cancelled) {
            result.outcome = SettingsBatteryOutcome::Cancelled;
            result.code = -1;
        } else if (!completed) {
            invocation->completed = true;
            result.outcome = SettingsBatteryOutcome::TimedOut;
            result.code = -1;
        } else {
            result.code = invocation->code;
            result.payload = invocation->payload;
            result.outcome = result.code == 0 ? SettingsBatteryOutcome::Success
                                              : SettingsBatteryOutcome::Failed;
        }
        invocation_lock.unlock();

        bool deliver = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            deliver = !shutting_down && alive->load() &&
                      active_generation == request_generation;
        }

        if (deliver) {
            const auto lifetime = alive;
            const auto dispatcher = post;
            try {
                dispatcher([lifetime,
                            invocation,
                            completion = std::move(completion),
                            result]() mutable {
                    if (!lifetime->load()) return;
                    {
                        std::lock_guard<std::mutex> lock(invocation->mutex);
                        if (invocation->cancelled &&
                            result.outcome != SettingsBatteryOutcome::Cancelled)
                            return;
                    }
                    try {
                        completion(result);
                    } catch (...) {
                    }
                });
            } catch (...) {
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (active_generation == request_generation) {
                pending = false;
                active_invocation.reset();
            }
        }
    }

    void cancel()
    {
        std::shared_ptr<Invocation> invocation;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!pending) return;
            invocation = active_invocation;
        }
        if (!invocation) return;
        {
            std::lock_guard<std::mutex> lock(invocation->mutex);
            invocation->cancelled = true;
        }
        invocation->condition.notify_all();
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
        std::thread current;
        std::shared_ptr<Invocation> invocation;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shutting_down) return;
            shutting_down = true;
            alive->store(false);
            current = std::move(worker);
            invocation = active_invocation;
        }

        if (current.joinable()) {
            if (invocation) {
                std::lock_guard<std::mutex> lock(invocation->mutex);
                invocation->cancelled = true;
                invocation->condition.notify_all();
            }
            if (current.get_id() == std::this_thread::get_id())
                current.detach();
            else
                current.join();
        }

        std::lock_guard<std::mutex> lock(mutex);
        pending = false;
        active_invocation.reset();
    }
};

SettingsBatteryRequestCoordinator::SettingsBatteryRequestCoordinator(
    SettingsBatteryApi api,
    Post post,
    std::chrono::milliseconds timeout)
    : impl_(std::make_shared<Impl>(std::move(api), std::move(post), timeout))
{
}

SettingsBatteryRequestCoordinator::~SettingsBatteryRequestCoordinator()
{
    shutdown();
}

bool SettingsBatteryRequestCoordinator::read(Completion completion)
{
    return impl_ && impl_->start(SettingsBatteryOperation::Read, -1, std::move(completion));
}

bool SettingsBatteryRequestCoordinator::calibrate(int command_index, Completion completion)
{
    return impl_ &&
           impl_->start(SettingsBatteryOperation::Calibrate,
                        command_index,
                        std::move(completion));
}

void SettingsBatteryRequestCoordinator::cancel()
{
    if (impl_) impl_->cancel();
}

void SettingsBatteryRequestCoordinator::shutdown()
{
    if (impl_) impl_->shutdown();
}

bool SettingsBatteryRequestCoordinator::pending() const
{
    if (!impl_) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pending;
}

std::uint64_t SettingsBatteryRequestCoordinator::generation() const
{
    if (!impl_) return 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->active_generation;
}
