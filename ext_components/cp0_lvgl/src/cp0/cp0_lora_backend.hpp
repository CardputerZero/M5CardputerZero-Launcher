#pragma once

#include "cp0_lvgl_app.h"

namespace cp0_lora_backend {

bool initialize();
void request_stop() noexcept;
void clear_stop() noexcept;
void poll();
void get_info(cp0_lora_info_t *info, bool drain_events);
bool send_text(const char *payload);
void start_receive();
void set_tx_mode(bool enabled);
void shutdown();

} // namespace cp0_lora_backend
