#include "settings_battery_api.hpp"
#include "settings_battery_info_model.hpp"

#include "hal_lvgl_bsp.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_bq27220_api;

namespace {

using Result = SettingsBatteryOperationResult;

struct FakeBackend {
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<SettingsBatteryApi::Arguments> arguments;
    std::vector<SettingsBatteryApi::Callback> callbacks;

    SettingsBatteryApi api()
    {
        return SettingsBatteryApi(
            [this](SettingsBatteryApi::Arguments arguments,
                   SettingsBatteryApi::Callback callback) {
                std::lock_guard<std::mutex> lock(mutex);
                this->arguments.push_back(std::move(arguments));
                this->callbacks.push_back(std::move(callback));
                condition.notify_all();
            });
    }

    void wait_for_arguments(std::size_t expected)
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(condition.wait_for(lock, std::chrono::seconds(2), [&] {
            return arguments.size() >= expected;
        }));
    }

    void complete(std::size_t index, int code, const std::string &payload)
    {
        SettingsBatteryApi::Callback callback;
        {
            std::lock_guard<std::mutex> lock(mutex);
            assert(index < callbacks.size());
            callback = callbacks[index];
        }
        callback(code, payload);
    }

    SettingsBatteryApi::Arguments arguments_at(std::size_t index)
    {
        std::lock_guard<std::mutex> lock(mutex);
        assert(index < arguments.size());
        return arguments[index];
    }
};

void test_model()
{
    SettingsBatteryInfoModel model;
    assert(!model.valid());
    assert(model.labels()[0] == "Battery: --%");
    assert(model.labels()[4] == "Remaining: --mAh");

    assert(model.update(0, "4012,-125,253,87,1000,1200,0,-100,1"));
    assert(model.valid());
    assert(model.snapshot().remain_mah == 1000);
    assert(model.snapshot().full_mah == 1200);
    assert(model.labels()[0] == "Battery: 87%");
    assert(model.labels()[1] == "Temp: 25.3C");
    assert(model.labels()[2] == "Current: -125mA");
    assert(model.labels()[3] == "Voltage: 4.01V");
    assert(model.labels()[4] == "Remaining: 1000mAh");
    assert(model.labels()[5] == "Full: 1200mAh");

    assert(model.update(0, "4012,-2147483648,253,87,1000,1200,0,-100,1"));
    assert(model.labels()[2] == "Current: --mA");

    assert(!model.update(-1, ""));
    assert(!model.valid());
    assert(model.labels()[0] == "Battery: --%");
    assert(model.labels()[4] == "Remaining: --mAh");
    assert(model.status_text() == "Battery read failed");

    assert(!model.update(0, "4012,0,253,87,1300,1200,0,0,1"));
    assert(!model.valid());
    assert(model.status_text() == "Invalid battery data");
    assert(!model.update(0, "4012,0,253,87,1000,1200,0,0,1junk"));
    assert(!model.update(0, "4012,0,253,101,1000,1200,0,0,1"));
    assert(!model.update(0, "4012,0,253,87,1000,1200,0,0,0"));
    assert(!model.update(0, "4012,0,253,87,1000,1200,0,-2147483648,1"));
}

void wait_for_posted(std::mutex &mutex,
                     std::condition_variable &condition,
                     std::vector<std::function<void()>> &posted,
                     std::size_t expected)
{
    std::unique_lock<std::mutex> lock(mutex);
    assert(condition.wait_for(lock, std::chrono::seconds(2), [&] {
        return posted.size() >= expected;
    }));
}

void run_posted(std::mutex &mutex,
                std::vector<std::function<void()>> &posted,
                std::size_t index)
{
    std::function<void()> task;
    {
        std::lock_guard<std::mutex> lock(mutex);
        assert(index < posted.size());
        task = std::move(posted[index]);
    }
    task();
}

bool wait_for_not_pending(SettingsBatteryRequestCoordinator &coordinator)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (coordinator.pending() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return !coordinator.pending();
}

void test_api_and_requests()
{
    FakeBackend backend;
    for (int index = 0; index < 4; ++index) {
        bool callback_called = false;
        SettingsBatteryApi api = backend.api();
        api.calibrate(index, [&](int code, std::string) {
            callback_called = true;
            assert(code == 0);
        });
        assert(backend.arguments_at(static_cast<std::size_t>(index)) ==
               SettingsBatteryApi::Arguments({"Calibrate", std::to_string(index)}));
        backend.complete(static_cast<std::size_t>(index), 0, "");
        assert(callback_called);
    }

    int invalid_calls = 0;
    SettingsBatteryApi api = backend.api();
    api.calibrate(4, [&](int code, std::string) {
        ++invalid_calls;
        assert(code != 0);
    });
    assert(invalid_calls == 1);

    std::mutex post_mutex;
    std::condition_variable post_condition;
    std::vector<std::function<void()>> posted;
    auto post = [&](std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(post_mutex);
            posted.push_back(std::move(task));
        }
        post_condition.notify_all();
        return true;
    };

    FakeBackend request_backend;
    SettingsBatteryRequestCoordinator coordinator(
        request_backend.api(),
        post,
        std::chrono::milliseconds(100));

    std::vector<Result> results;
    assert(coordinator.read([&](const Result &result) { results.push_back(result); }));
    assert(coordinator.pending());
    assert(!coordinator.read([&](const Result &) {}));
    request_backend.wait_for_arguments(1);
    assert(request_backend.arguments_at(0) == SettingsBatteryApi::Arguments({"Read"}));
    request_backend.complete(0, 0, "4012,-125,253,87,1000,1200,0,-100,1");
    wait_for_posted(post_mutex, post_condition, posted, 1);
    assert(results.empty());
    run_posted(post_mutex, posted, 0);
    assert(results.size() == 1);
    assert(results[0].outcome == SettingsBatteryOutcome::Success);
    assert(results[0].generation == 1);
    assert(!coordinator.pending());

    assert(coordinator.calibrate(2, [&](const Result &result) { results.push_back(result); }));
    request_backend.wait_for_arguments(2);
    assert(request_backend.arguments_at(1) ==
           SettingsBatteryApi::Arguments({"Calibrate", "2"}));
    request_backend.complete(1, -17, "calibration failed");
    wait_for_posted(post_mutex, post_condition, posted, 2);
    run_posted(post_mutex, posted, 1);
    assert(results.size() == 2);
    assert(results[1].outcome == SettingsBatteryOutcome::Failed);
    assert(results[1].code == -17);

    assert(wait_for_not_pending(coordinator));
    assert(coordinator.read([&](const Result &result) { results.push_back(result); }));
    request_backend.wait_for_arguments(3);
    wait_for_posted(post_mutex, post_condition, posted, 3);
    run_posted(post_mutex, posted, 2);
    assert(results.size() == 3);
    assert(results[2].outcome == SettingsBatteryOutcome::TimedOut);

    assert(wait_for_not_pending(coordinator));
    assert(coordinator.read([&](const Result &result) { results.push_back(result); }));
    request_backend.wait_for_arguments(4);
    coordinator.cancel();
    wait_for_posted(post_mutex, post_condition, posted, 4);
    run_posted(post_mutex, posted, 3);
    assert(results.size() == 4);
    assert(results[3].outcome == SettingsBatteryOutcome::Cancelled);

    assert(wait_for_not_pending(coordinator));
    assert(coordinator.read([&](const Result &result) { results.push_back(result); }));
    request_backend.wait_for_arguments(5);
    coordinator.shutdown();
    const std::size_t result_count = results.size();
    {
        std::lock_guard<std::mutex> lock(post_mutex);
        for (std::size_t index = 4; index < posted.size(); ++index)
            posted[index]();
    }
    assert(results.size() == result_count);
}

} // namespace

int main()
{
    test_model();
    test_api_and_requests();
    return 0;
}
