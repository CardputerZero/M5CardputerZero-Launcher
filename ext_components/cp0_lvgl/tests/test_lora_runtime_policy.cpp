#include "cp0_lora_runtime_policy.hpp"

#include <cassert>

int main()
{
    using namespace cp0_lora_runtime_policy;

    assert(tx_timeout_for_airtime_us(0) == TX_TIMEOUT_MS);
    assert(tx_timeout_for_airtime_us(1000000) == TX_TIMEOUT_MS);
    assert(tx_timeout_for_airtime_us(5000000) == 9000);
    assert(tx_timeout_for_airtime_us(5300000) == 9450);

    assert(!should_timeout_transmit(false, true, true, 100, 4100));
    assert(!should_timeout_transmit(true, false, true, 100, 4100));
    assert(!should_timeout_transmit(true, true, false, 100, 4100));
    assert(!should_timeout_transmit(true, true, true, 0, 5000));
    assert(!should_timeout_transmit(true, true, true, 100, 4099));
    assert(should_timeout_transmit(true, true, true, 100, 4100));
    assert(!should_timeout_transmit(true, true, true, 100, 8099, 8000));
    assert(should_timeout_transmit(true, true, true, 100, 8100, 8000));
    assert(should_timeout_transmit(true, true, true, 100, 4100, 0));
    assert(!should_timeout_transmit(true, true, true, 4100, 100));

    assert(!should_send_demo(false, true, false, 100, 2100));
    assert(!should_send_demo(true, false, false, 100, 2100));
    assert(!should_send_demo(true, true, true, 100, 2100));
    assert(!should_send_demo(true, true, false, 100, 2099));
    assert(should_send_demo(true, true, false, 100, 2100));
    assert(!should_send_demo(true, true, false, 2100, 100));

    return 0;
}
