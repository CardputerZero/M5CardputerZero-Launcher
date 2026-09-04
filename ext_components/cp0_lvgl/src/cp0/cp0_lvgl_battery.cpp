#include "hal_lvgl_bsp.h"
#include "lvgl/lvgl.h"
#include "cp0_lvgl.h"
#include "cp0_lvgl_app.h"
#include "../cp0_battery_lifecycle.hpp"
#include "../cp0_battery_publish_gate.hpp"
#include "../cp0_signal_registration.hpp"

#include <functional>
#include <memory>

namespace {

void publish_battery()
{
    static cp0::battery::PublishGate gate;
    gate.run([] {
        const uint32_t event_code = lv_c_event[CP0_C_EVENT_BATTERY];
        if (event_code == 0) return;
        cp0_battery_info_t info = cp0_battery_read();
        lv_obj_t *root = lv_display_get_screen_active(nullptr);
        if (root)
            lv_obj_send_event(root, static_cast<lv_event_code_t>(event_code), &info);
    });
}

void battery_timer_cb(lv_timer_t *)
{
    publish_battery();
}

cp0::battery::Lifecycle &battery_lifecycle()
{
    static cp0::battery::Lifecycle lifecycle;
    return lifecycle;
}

} // namespace

extern "C" void init_battery()
{
    battery_lifecycle().start(
        [] {
            using Registration = cp0::SignalRegistration<decltype(cp0_signal_battery_pub)>;
            auto registration = std::make_shared<Registration>();
            const bool registered = registration->replace(
                cp0_signal_battery_pub, [](std::function<void()>) { publish_battery(); });
            return cp0::battery::LifecycleResource{
                registered, [registration] { registration->reset(); }};
        },
        [] {
            lv_timer_t *timer = lv_timer_create(battery_timer_cb, 3000, nullptr);
            return cp0::battery::LifecycleResource{
                timer != nullptr, [timer] {
                    cp0::battery::release_timer_if_runtime_active(
                        timer, lv_is_initialized(), lv_timer_delete);
                }};
        });
}

extern "C" void deinit_battery()
{
    battery_lifecycle().stop();
    deinit_bq27220();
}
