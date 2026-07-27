#include "wizard_model.h"

#include <arpa/inet.h>
#include <cctype>
#include <cstdlib>

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
