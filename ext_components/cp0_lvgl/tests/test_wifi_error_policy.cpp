#include "../src/cp0/cp0_wifi_error_policy.hpp"

#include <cassert>

int main()
{
    assert(cp0::wifi::classify_command_failure("Secrets were required") ==
           CP0_WIFI_ERROR_AUTH);
    assert(cp0::wifi::classify_command_failure("No network with SSID found") ==
           CP0_WIFI_ERROR_NOT_FOUND);
    assert(cp0::wifi::classify_command_failure("IP configuration timed out (DHCP)") ==
           CP0_WIFI_ERROR_IP_CONFIG);
    assert(cp0::wifi::classify_command_failure("Wi-Fi radio is disabled") ==
           CP0_WIFI_ERROR_RADIO_OFF);
    assert(cp0::wifi::classify_command_failure("NetworkManager is not running") ==
           CP0_WIFI_ERROR_SERVICE);
}
