#include "cp0_lora_backend.hpp"

#include "cp0_lvgl_app.h"
#include "../cp0_lora_contract.hpp"
#include "../cp0_callback_contract.hpp"
#include "../cp0_callback_result.hpp"
#include "../cp0_signal_registration.hpp"
#include "hal_lvgl_bsp.h"

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>

namespace {

cp0::lora::Operations hardware_operations()
{
    cp0::lora::Operations operations;
    operations.initialize = cp0_lora_backend::initialize;
    operations.poll = cp0_lora_backend::poll;
    operations.read_info = cp0_lora_backend::get_info;
    operations.send_text = [](const std::string &payload) {
        return cp0_lora_backend::send_text(payload.c_str());
    };
    operations.start_receive = cp0_lora_backend::start_receive;
    operations.set_tx_mode = cp0_lora_backend::set_tx_mode;
    operations.shutdown = cp0_lora_backend::shutdown;
    return operations;
}

std::shared_ptr<cp0::lora::Service> &lora_service()
{
    static auto service = std::make_shared<cp0::lora::Service>(hardware_operations());
    return service;
}

cp0::SignalRegistration<decltype(cp0_signal_lora_api)> &lora_registration()
{
    static cp0::SignalRegistration<decltype(cp0_signal_lora_api)> registration;
    return registration;
}

} // namespace

extern "C" void init_lora(void)
{
    const auto active_service = lora_service();
    lora_registration().replace(
        cp0_signal_lora_api,
        [active_service](std::list<std::string> arguments,
                         std::function<void(int, std::string)> callback) {
            cp0::CallbackResult completion(std::move(callback));
            completion.guard(-1, "lora api failure", [&] {
                const cp0::lora::Result result = active_service->call(arguments);
                completion.complete(result.code, result.payload);
            });
        });
}

extern "C" void cp0_lora_request_stop(void)
{
    cp0_lora_backend::request_stop();
}

extern "C" void cp0_lora_clear_stop(void)
{
    cp0_lora_backend::clear_stop();
}

extern "C" void deinit_lora(void) noexcept
{
    try {
        lora_registration().reset();
    } catch (...) {
    }
    try {
        (void)lora_service()->call({"Shutdown"});
    } catch (...) {
    }
}
