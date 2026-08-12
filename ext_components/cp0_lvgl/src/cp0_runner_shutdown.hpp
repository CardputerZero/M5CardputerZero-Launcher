#pragma once

#include <utility>

extern "C" void deinit_battery(void);
extern "C" void deinit_audio(void) noexcept;
extern "C" void deinit_camera(void) noexcept;
extern "C" void deinit_input(void);
extern "C" void deinit_lora(void) noexcept;
extern "C" void deinit_pty(void) noexcept;
extern "C" void deinit_rpc(void) noexcept;
extern "C" void deinit_sudo(void) noexcept;
extern "C" void deinit_wifi(void);

namespace cp0::runner {

template <typename StopSudo, typename StopRpc, typename StopCamera,
          typename StopAudio, typename StopPty, typename StopInput, typename StopWifi,
          typename StopLora, typename StopBattery,
          typename DeinitializeLvgl>
void shutdown_services(StopSudo &&stop_sudo, StopRpc &&stop_rpc,
                       StopCamera &&stop_camera, StopAudio &&stop_audio,
                       StopPty &&stop_pty,
                       StopInput &&stop_input, StopWifi &&stop_wifi,
                       StopLora &&stop_lora, StopBattery &&stop_battery,
                       DeinitializeLvgl &&deinitialize_lvgl) noexcept
{
    try {
        std::forward<StopSudo>(stop_sudo)();
    } catch (...) {
    }
    try {
        std::forward<StopRpc>(stop_rpc)();
    } catch (...) {
    }
    try {
        std::forward<StopCamera>(stop_camera)();
    } catch (...) {
    }
    try {
        std::forward<StopAudio>(stop_audio)();
    } catch (...) {
    }
    try {
        std::forward<StopPty>(stop_pty)();
    } catch (...) {
    }
    try {
        std::forward<StopInput>(stop_input)();
    } catch (...) {
    }
    try {
        std::forward<StopWifi>(stop_wifi)();
    } catch (...) {
    }
    try {
        std::forward<StopLora>(stop_lora)();
    } catch (...) {
    }
    try {
        std::forward<StopBattery>(stop_battery)();
    } catch (...) {
    }
    try {
        std::forward<DeinitializeLvgl>(deinitialize_lvgl)();
    } catch (...) {
    }
}

} // namespace cp0::runner
