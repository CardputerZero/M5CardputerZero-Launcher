#include "wizard_model.h"

#include <iostream>

bool test_wizard_model()
{
    using namespace launch_wizard;
    bool passed = true;
    std::string error;
    const auto expect = [&passed](bool condition, const char *message) {
        if (!condition) {
            std::cerr << message << '\n';
            passed = false;
        }
    };

    WizardModel model;
    expect(model.screen == Screen::Welcome, "model must start at welcome");
    expect(model.current_timezone().name == std::string("Etc/GMT-8"),
           "default timezone must be UTC+8");
    expect(validate_username("cardputer", error), "ordinary username rejected");
    expect(!validate_username("root", error), "root username accepted");
    expect(validate_hostname("CardputerZero", error), "default hostname rejected");
    expect(!validate_hostname("bad host", error), "hostname with spaces accepted");
    expect(validate_wifi_ssid("Hidden WiFi", error), "valid Wi-Fi SSID rejected");
    expect(!validate_wifi_ssid("", error), "empty Wi-Fi SSID accepted");
    expect(!validate_wifi_ssid(std::string(33, 'x'), error), "oversized Wi-Fi SSID accepted");
    expect(valid_ipv4_cidr("192.168.1.4/24"), "valid CIDR rejected");
    expect(!valid_ipv4_cidr("192.168.1.4/33"), "invalid CIDR accepted");

    model.ethernet_dhcp = false;
    expect(validate_ethernet_config(model).empty(), "default static network rejected");
    model.ethernet_gateway = "999.1.1.1";
    expect(validate_ethernet_config(model) == "Invalid gateway",
           "invalid gateway not diagnosed");
    return passed;
}
