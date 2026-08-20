#include "cp0_lora_runtime_policy.hpp"

#include <limits>

namespace cp0_lora_runtime_policy {
namespace {

bool interval_elapsed(uint64_t start_ms, uint64_t now_ms, uint64_t interval_ms)
{
    return now_ms >= start_ms && now_ms - start_ms >= interval_ms;
}

}  // namespace

uint64_t tx_timeout_for_airtime_us(uint64_t airtime_us) noexcept
{
    if (airtime_us == 0) return TX_TIMEOUT_MS;

    const uint64_t airtime_ms = airtime_us / 1000 + (airtime_us % 1000 != 0 ? 1 : 0);
    const uint64_t guard_ms   = airtime_ms / 2;
    const uint64_t max_value  = std::numeric_limits<uint64_t>::max();
    if (airtime_ms > max_value - guard_ms - TX_AIRTIME_GUARD_MS) return max_value;

    const uint64_t guarded = airtime_ms + guard_ms + TX_AIRTIME_GUARD_MS;
    return guarded > TX_TIMEOUT_MS ? guarded : TX_TIMEOUT_MS;
}

bool should_timeout_transmit(bool initialized, bool tx_in_progress, bool radio_available, uint64_t tx_start_ms,
                             uint64_t now_ms, uint64_t timeout_ms)
{
    return initialized && tx_in_progress && radio_available && tx_start_ms != 0 &&
           interval_elapsed(tx_start_ms, now_ms, timeout_ms == 0 ? TX_TIMEOUT_MS : timeout_ms);
}

bool should_send_demo(bool initialized, bool tx_mode, bool tx_in_progress, uint64_t last_auto_tx_ms, uint64_t now_ms)
{
    return initialized && tx_mode && !tx_in_progress && interval_elapsed(last_auto_tx_ms, now_ms, AUTO_TX_INTERVAL_MS);
}

}  // namespace cp0_lora_runtime_policy
