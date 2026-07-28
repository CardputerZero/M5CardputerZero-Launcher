#include "wizard_model.h"
#include "wizard_input_context.hpp"

#include <iostream>
#include <set>

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
    expect(wizard_input_context(model) == KBD_INPUT_CONTEXT_NAVIGATION,
           "welcome must use navigation input context");
    model.screen = Screen::Hostname;
    expect(wizard_input_context(model) == KBD_INPUT_CONTEXT_TEXT,
           "hostname must use text input context");
    model.screen = Screen::EthernetConfig;
    model.ethernet_dhcp = true;
    expect(wizard_input_context(model) == KBD_INPUT_CONTEXT_NAVIGATION,
           "DHCP selector must use navigation input context");
    model.ethernet_dhcp = false;
    model.ethernet_focus = 1;
    expect(wizard_input_context(model) == KBD_INPUT_CONTEXT_TEXT,
           "static address must use text input context");
    model.screen = Screen::Welcome;
    expect(model.screen == Screen::Welcome, "model must start at welcome");
    expect(model.password == "pi" && model.confirm == "pi",
           "default account credentials must be pi/pi");
    expect(model.current_timezone().name == std::string("Asia/Shanghai"),
           "default timezone must be Shanghai");
    expect(model.current_timezone().label == std::string("UTC+08:00"),
           "default timezone label must be UTC+08:00");
    std::set<std::string> timezone_labels;
    for (const Timezone &timezone : kTimezones) {
        const std::string label = timezone.label;
        expect(label.size() == 9 && label.compare(0, 3, "UTC") == 0 &&
                   (label[3] == '+' || label[3] == '-') && label[6] == ':',
               "timezone label must use UTC+HH:MM or UTC-HH:MM");
        expect(timezone_labels.insert(label).second,
               "timezone labels must be unique");
    }
    expect(std::string(kTimezones[6].name) == "America/New_York" &&
               std::string(kTimezones[6].label) == "UTC-05:00",
           "UTC-05:00 must retain a DST-capable IANA mapping");
    expect(std::string(kTimezones[kTimezoneCount - 1].name) == "Pacific/Kiritimati" &&
               std::string(kTimezones[kTimezoneCount - 1].label) == "UTC+14:00",
           "UTC+14:00 timezone missing");
    expect(validate_username("cardputer", error), "ordinary username rejected");
    expect(!validate_username("root", error), "root username accepted");
    expect(validate_hostname("CardputerZero", error), "default hostname rejected");
    expect(!validate_hostname("bad host", error), "hostname with spaces accepted");
    expect(validate_wifi_ssid("Hidden WiFi", error), "valid Wi-Fi SSID rejected");
    expect(!validate_wifi_ssid("", error), "empty Wi-Fi SSID accepted");
    expect(!validate_wifi_ssid(std::string(33, 'x'), error), "oversized Wi-Fi SSID accepted");
    expect(classify_wifi_security("") == WifiSecurity::Open,
           "empty security must mean open Wi-Fi");
    expect(classify_wifi_security("WPA2 WPA3") == WifiSecurity::Personal,
           "WPA personal security not recognized");
    expect(classify_wifi_security("WPA2 802.1X") == WifiSecurity::Enterprise,
           "enterprise security not recognized");
    expect(validate_wifi_credentials("Open", "", "", false, error),
           "open network rejected");
    expect(!validate_wifi_credentials("Open", "", "unexpected", false, error),
           "open network accepted a password");
    expect(!validate_wifi_credentials("Home", "WPA2", "1234567", false, error),
           "7-byte WPA password accepted");
    expect(validate_wifi_credentials("Home", "WPA2", "12345678", false, error),
           "8-byte WPA password rejected");
    expect(validate_wifi_credentials("Home", "WPA3", std::string(63, 'p'), false, error),
           "63-byte WPA password rejected");
    expect(validate_wifi_credentials("Home", "WPA2", std::string(64, 'a'), false, error),
           "64-hex WPA key rejected");
    expect(!validate_wifi_credentials("Home", "WPA2", std::string(64, 'z'), false, error),
           "64-byte non-hex WPA password accepted");
    expect(validate_wifi_credentials("Home", "WPA2",
                                     std::string("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"),
                                     false, error),
           "8-byte UTF-8 WPA password rejected");
    expect(!validate_wifi_credentials("Home", "WPA2",
                                      std::string("\xC3\xA9\xC3\xA9\xC3\xA9"),
                                      false, error),
           "6-byte UTF-8 WPA password accepted");
    expect(validate_wifi_credentials("Home", "WPA2", std::string(63, '\x7f'), false, error),
           "63-byte WPA boundary rejected");
    expect(!validate_wifi_credentials("Corp", "WPA2 802.1X", "12345678", false, error),
           "enterprise Wi-Fi accepted");
    expect(validate_wifi_credentials("HiddenOpen", "", "", true, error),
           "hidden open network rejected");
    expect(validate_wifi_credentials("HiddenWpa", "", "12345678", true, error),
           "hidden WPA network rejected");
    WifiScanRetryPolicy retry_policy;
    for (int attempt = 1; attempt < kWifiMaxAutomaticScans; ++attempt)
        expect(retry_policy.observe(0, 0) == WifiScanDecision::Retry,
               "empty scan stopped before retry limit");
    expect(retry_policy.observe(0, 1) == WifiScanDecision::Results,
           "fourth scan results did not stop retries");
    retry_policy.reset();
    for (int attempt = 1; attempt < kWifiMaxAutomaticScans; ++attempt)
        expect(retry_policy.observe(0, 0) == WifiScanDecision::Retry,
               "persistent empty scan stopped too early");
    expect(retry_policy.observe(0, 0) == WifiScanDecision::Empty,
           "persistent empty scan did not converge to Empty");
    retry_policy.reset();
    expect(retry_policy.observe(-1, 0) == WifiScanDecision::Error,
           "scan error incorrectly retried");
    retry_policy.reset();
    expect(retry_policy.attempts() == 0 &&
               retry_policy.observe(0, 0) == WifiScanDecision::Retry,
           "manual rescan did not reset retry policy");
    expect(valid_ipv4_cidr("192.168.1.4/24"), "valid CIDR rejected");
    expect(!valid_ipv4_cidr("192.168.1.4/33"), "invalid CIDR accepted");

    model.ethernet_dhcp = false;
    expect(validate_ethernet_config(model).empty(), "default static network rejected");
    model.ethernet_gateway = "999.1.1.1";
    expect(validate_ethernet_config(model) == "Invalid gateway",
           "invalid gateway not diagnosed");
    return passed;
}
