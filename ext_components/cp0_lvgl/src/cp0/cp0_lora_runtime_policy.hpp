#pragma once

#include <cstdint>

namespace cp0_lora_runtime_policy {

constexpr uint64_t TX_TIMEOUT_MS       = 4000;
constexpr uint64_t TX_AIRTIME_GUARD_MS = 1500;
constexpr uint64_t AUTO_TX_INTERVAL_MS = 2000;

// Return a watchdog interval long enough for the packet to finish in the air,
// with margin for the scheduler and the radio state transition. Keep the
// previous four-second minimum for short packets and unknown airtime.
uint64_t tx_timeout_for_airtime_us(uint64_t airtime_us) noexcept;

bool should_timeout_transmit(bool initialized, bool tx_in_progress, bool radio_available, uint64_t tx_start_ms,
                             uint64_t now_ms, uint64_t timeout_ms = TX_TIMEOUT_MS);

bool should_send_demo(bool initialized, bool tx_mode, bool tx_in_progress, uint64_t last_auto_tx_ms, uint64_t now_ms);

}  // namespace cp0_lora_runtime_policy
