#include "wizard_model.h"

#include <arpa/inet.h>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace launch_wizard {

const Timezone &WizardModel::current_timezone() const
{
    return kTimezones[timezone_index];
}

bool validate_username(const std::string &name, std::string &error)
{
    if (name.empty()) { error = "Username required"; return false; }
    if (name.size() > 32) { error = "Username too long"; return false; }
    if (name == "root") { error = "Do not create root"; return false; }
    const unsigned char first = static_cast<unsigned char>(name[0]);
    if (!(std::islower(first) || name[0] == '_')) {
        error = "Use lowercase user name"; return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        bool ok = std::islower(ch) || std::isdigit(ch) || name[i] == '_' || name[i] == '-';
        if (name[i] == '$' && i == name.size() - 1) ok = true;
        if (!ok) { error = "Invalid username char"; return false; }
    }
    return true;
}

bool validate_password(const std::string &password, std::string &error)
{
    if (password.empty()) { error = "Password required"; return false; }
    for (char ch : password) {
        if (ch == '\n' || ch == '\r' || ch == ':' || static_cast<unsigned char>(ch) < 0x20) {
            error = "Password has bad char"; return false;
        }
    }
    return true;
}

bool validate_hostname(const std::string &name, std::string &error)
{
    if (name.empty()) { error = "Hostname required"; return false; }
    if (name.size() > 63) { error = "Hostname too long"; return false; }
    for (char ch : name) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (!(std::isalnum(value) || ch == '-')) {
            error = "Invalid hostname char"; return false;
        }
    }
    if (name.front() == '-' || name.back() == '-') {
        error = "Bad hostname dashes"; return false;
    }
    return true;
}

bool validate_wifi_ssid(const std::string &ssid, std::string &error)
{
    if (ssid.empty()) { error = "SSID is required"; return false; }
    if (ssid.size() > 32) { error = "SSID is too long"; return false; }
    for (char ch : ssid) {
        if (ch == '\0' || ch == '\n' || ch == '\r') {
            error = "SSID has invalid characters"; return false;
        }
    }
    return true;
}

WifiSecurity classify_wifi_security(const std::string &security)
{
    std::string normalized = security;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (normalized.empty() || normalized == "--" || normalized == "OPEN" ||
        normalized == "NONE")
        return WifiSecurity::Open;
    if (normalized.find("802.1X") != std::string::npos ||
        normalized.find("8021X") != std::string::npos ||
        normalized.find("EAP") != std::string::npos ||
        normalized.find("ENTERPRISE") != std::string::npos)
        return WifiSecurity::Enterprise;
    if (normalized.find("WPA") != std::string::npos ||
        normalized.find("SAE") != std::string::npos ||
        normalized.find("PSK") != std::string::npos)
        return WifiSecurity::Personal;
    if (normalized == "UNKNOWN")
        return WifiSecurity::Unknown;
    return WifiSecurity::Unsupported;
}

WifiScanDecision WifiScanRetryPolicy::observe(int error, std::size_t network_count)
{
    ++attempts_;
    if (network_count != 0) return WifiScanDecision::Results;
    if (attempts_ < kWifiMaxAutomaticScans)
        return WifiScanDecision::Retry;
    return error != 0 ? WifiScanDecision::Error : WifiScanDecision::Empty;
}

bool validate_wifi_credentials(const std::string &ssid,
                               const std::string &security,
                               const std::string &password,
                               bool hidden,
                               std::string &error)
{
    if (!validate_wifi_ssid(ssid, error)) return false;
    WifiSecurity kind = hidden ? WifiSecurity::Unknown : classify_wifi_security(security);
    if (kind == WifiSecurity::Enterprise) {
        error = "Enterprise Wi-Fi is not supported";
        return false;
    }
    if (kind == WifiSecurity::Unsupported) {
        error = "Unsupported Wi-Fi security";
        return false;
    }
    if (kind == WifiSecurity::Open || (kind == WifiSecurity::Unknown && password.empty())) {
        if (!password.empty()) {
            error = "Open Wi-Fi does not use a password";
            return false;
        }
        return true;
    }
    if (password.size() >= 8 && password.size() <= 63)
        return true;
    if (password.size() == 64 &&
        std::all_of(password.begin(), password.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        }))
        return true;
    error = "WPA password must be 8-63 chars or 64 hex";
    return false;
}

bool valid_ipv4(const std::string &value)
{
    in_addr address{};
    return inet_pton(AF_INET, value.c_str(), &address) == 1;
}

bool valid_ipv4_cidr(const std::string &value)
{
    const std::size_t slash = value.find('/');
    if (slash == std::string::npos || !valid_ipv4(value.substr(0, slash))) return false;
    const std::string prefix = value.substr(slash + 1);
    if (prefix.empty() || prefix.find_first_not_of("0123456789") != std::string::npos) return false;
    const int bits = std::atoi(prefix.c_str());
    return bits >= 0 && bits <= 32;
}

std::string validate_ethernet_config(const WizardModel &model)
{
    if (model.ethernet_dhcp) return {};
    if (!valid_ipv4_cidr(model.ethernet_address)) return "Invalid IP/CIDR";
    if (!valid_ipv4(model.ethernet_gateway)) return "Invalid gateway";
    if (!model.ethernet_dns.empty() && !valid_ipv4(model.ethernet_dns)) return "Invalid DNS";
    return {};
}

}  // namespace launch_wizard
