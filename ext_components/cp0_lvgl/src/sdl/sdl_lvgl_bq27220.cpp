#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "../cp0_battery_api_contract.hpp"
#include "../cp0_callback_result.hpp"
#include "../cp0_battery_codec.hpp"
#include "../cp0_battery_lifecycle.hpp"
#include "../cp0_callback_contract.hpp"
#include "../cp0_signal_registration.hpp"

#include <ctime>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace {

cp0::battery::Lifecycle &bq_lifecycle()
{
    static cp0::battery::Lifecycle lifecycle;
    return lifecycle;
}

cp0_battery_info_t simulated_battery_info()
{
    cp0_battery_info_t info{};
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    constexpr int kMinSoc = 55;
    constexpr int kMaxSoc = 96;
    constexpr int kRange = kMaxSoc - kMinSoc;
    const long tick_100ms = now.tv_sec * 10L + now.tv_nsec / 100000000L;
    const int step = static_cast<int>(tick_100ms % (kRange * 2));
    const int soc = (step <= kRange) ? (kMaxSoc - step) : (kMinSoc + step - kRange);

    info.voltage_mv = 3300 + soc * 9;
    info.current_ma = soc < 50 ? 200 : -200;
    info.temperature_c10 = 350;
    info.soc = soc;
    info.remain_mah = soc * 30;
    info.full_mah = 3000;
    info.flags = soc < 50 ? 1 : 0;
    info.avg_current_ma = info.current_ma;
    info.valid = 1;
    return info;
}

class Bq27220System
{
public:
    using arg_t = std::list<std::string>;
    using callback_t = std::function<void(int, std::string)>;

    void api_call(arg_t arg, callback_t callback)
    {
        cp0::CallbackResult result(std::move(callback));
        result.guard(-1, "battery api failure", [&] {
            cp0::battery::ApiReply reply;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                reply = cp0::battery::dispatch_api_request(
                    arg, [this] { return read(); },
                    [this](int index) { return calibrate(index); });
            }
            result.complete(reply.code, reply.data);
        });
    }

    cp0_battery_info_t read()
    {
        return simulated_battery_info();
    }

    int calibrate(int command_index)
    {
        return (command_index >= 0 && command_index < 4) ? 0 : -1;
    }

private:
    std::mutex mutex_;

};

} // namespace

extern "C" {

cp0_battery_info_t cp0_battery_read(void)
{
    cp0_battery_info_t info{};
    try {
        cp0_signal_bq27220_api({"Read"}, [&](int code, std::string data) {
            if (code == 0) cp0::battery::decode_info(data, &info);
        });
    } catch (...) {
        info = {};
    }
    return info;
}

int cp0_bq27220_calibrate(int command_index)
{
    int ret = -1;
    try {
        cp0_signal_bq27220_api({"Calibrate", std::to_string(command_index)}, [&](int code, std::string data) {
            (void)data;
            ret = code;
        });
    } catch (...) {
        ret = -1;
    }
    return ret;
}

void init_bq27220(void)
{
    bq_lifecycle().start(
        [] {
            auto bq27220 = std::make_shared<Bq27220System>();
            using Registration = cp0::SignalRegistration<decltype(cp0_signal_bq27220_api)>;
            auto registration = std::make_shared<Registration>();
            const bool registered = registration->replace(
                cp0_signal_bq27220_api,
                [bq27220](std::list<std::string> arg,
                          std::function<void(int, std::string)> callback) {
                    bq27220->api_call(std::move(arg), std::move(callback));
                });
            return cp0::battery::LifecycleResource{
                registered, [registration] { registration->reset(); }};
        },
        [] { return cp0::battery::LifecycleResource{true, {}}; });
}

void deinit_bq27220(void)
{
    bq_lifecycle().stop();
}

}
