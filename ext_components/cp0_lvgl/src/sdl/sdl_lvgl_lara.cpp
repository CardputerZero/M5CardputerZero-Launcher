#include "cp0_lvgl_app.h"
#include "../cp0_lora_contract.hpp"
#include "../cp0_callback_contract.hpp"
#include "../cp0_callback_result.hpp"
#include "../cp0_signal_registration.hpp"
#include "hal_lvgl_bsp.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>

class LoraSystem
{
public:
    LoraSystem()
    {
        cp0::lora::Operations operations;
        operations.initialize = [this] {
            initialized_ = true;
            hw_ready_ = true;
            std::snprintf(diag_, sizeof(diag_), "SDL simulated LoRa ready");
            return true;
        };
        operations.poll = [] {};
        operations.read_info = [this](cp0_lora_info_t *info, bool drain_events) {
            fill_info(info, drain_events);
        };
        operations.send_text = [this](const std::string &payload) {
            std::snprintf(last_tx_, sizeof(last_tx_), "%s", payload.c_str());
            std::snprintf(last_rx_, sizeof(last_rx_), "SDL echo: %.117s", last_tx_);
            has_sent_message_ = true;
            tx_event_ = true;
            rx_event_ = true;
            rssi_ = -42.0f;
            snr_ = 9.5f;
            std::snprintf(diag_, sizeof(diag_), "SDL simulated packet sent");
            return true;
        };
        operations.start_receive = [this] {
            tx_mode_ = false;
            std::snprintf(diag_, sizeof(diag_), "SDL simulated receive mode");
        };
        operations.set_tx_mode = [this](bool enabled) {
            tx_mode_ = enabled;
            std::snprintf(diag_, sizeof(diag_), tx_mode_ ? "SDL simulated TX mode" : "SDL simulated RX mode");
        };
        operations.shutdown = [this] {
            initialized_ = false;
            hw_ready_ = false;
            tx_mode_ = false;
            std::snprintf(diag_, sizeof(diag_), "SDL simulated LoRa shutdown");
        };
        service_ = std::make_unique<cp0::lora::Service>(std::move(operations));
    }

    void api_call(std::list<std::string> arg, std::function<void(int, std::string)> callback)
    {
        cp0::CallbackResult completion(std::move(callback));
        completion.guard(-1, "lora api failure", [&] {
            const cp0::lora::Result result = service_->call(arg);
            completion.complete(result.code, result.payload);
        });
    }

private:
    bool initialized_ = false;
    bool hw_ready_ = false;
    bool tx_mode_ = false;
    bool has_sent_message_ = false;
    bool rx_event_ = false;
    bool tx_event_ = false;
    float rssi_ = -48.0f;
    float snr_ = 7.0f;
    char last_rx_[128] = "SDL simulated receive buffer";
    char last_tx_[128] = "Hello from SDL LoRa";
    char diag_[256] = "SDL simulated LoRa idle";
    std::unique_ptr<cp0::lora::Service> service_;

    void fill_info(cp0_lora_info_t *info, bool drain_events)
    {
        if (!info) return;
        std::memset(info, 0, sizeof(*info));
        info->initialized = initialized_ ? 1 : 0;
        info->hw_ready = hw_ready_ ? 1 : 0;
        info->tx_mode = tx_mode_ ? 1 : 0;
        info->tx_in_progress = 0;
        info->has_sent_message = has_sent_message_ ? 1 : 0;
        info->rx_event = rx_event_ ? 1 : 0;
        info->tx_event = tx_event_ ? 1 : 0;
        std::snprintf(info->spi_device, sizeof(info->spi_device), "sdl://lora");
        std::snprintf(info->last_rx, sizeof(info->last_rx), "%s", last_rx_);
        std::snprintf(info->last_tx, sizeof(info->last_tx), "%s", last_tx_);
        std::snprintf(info->diag, sizeof(info->diag), "%s", diag_);
        std::snprintf(info->probe_summary, sizeof(info->probe_summary), "SDL simulated SPI/GPIO");
        std::snprintf(info->probe_display, sizeof(info->probe_display), "LoRa: SDL simulation");
        std::snprintf(info->pi4io_status, sizeof(info->pi4io_status), "SDL no-op PI4IO");
        info->rssi = rssi_;
        info->snr = snr_;
        if (drain_events) {
            rx_event_ = false;
            tx_event_ = false;
        }
    }

};

namespace {

std::shared_ptr<LoraSystem> &lora_system()
{
    static auto lora = std::make_shared<LoraSystem>();
    return lora;
}

cp0::SignalRegistration<decltype(cp0_signal_lora_api)> &lora_registration()
{
    static cp0::SignalRegistration<decltype(cp0_signal_lora_api)> registration;
    return registration;
}

} // namespace

extern "C" void init_lora(void)
{
    const auto active_lora = lora_system();
    lora_registration().replace(cp0_signal_lora_api, [active_lora](std::list<std::string> arg, std::function<void(int, std::string)> callback) {
        active_lora->api_call(arg, callback);
    });
}

extern "C" void cp0_lora_request_stop(void)
{
}

extern "C" void cp0_lora_clear_stop(void)
{
}

extern "C" void deinit_lora(void) noexcept
{
    try {
        lora_registration().reset();
    } catch (...) {
    }
    try {
        lora_system()->api_call({"Shutdown"}, nullptr);
    } catch (...) {
    }
}
