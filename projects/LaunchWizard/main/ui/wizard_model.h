#ifndef LAUNCH_WIZARD_WIZARD_MODEL_H
#define LAUNCH_WIZARD_WIZARD_MODEL_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace launch_wizard {

enum class Screen {
    Welcome,
    TimezoneList,
    Hostname,
    Account,
    Network,
    EthernetConfig,
    WifiList,
    WifiPassword,
    ManualTime,
    Ssh,
    Applying,
    RestartPrompt,
    Restarting,
    ApplyError,
};

struct Timezone {
    const char *name;
    const char *offset;
};

struct WifiNetwork {
    std::string ssid;
    int signal = 0;
};

struct WifiConnectionStatus {
    bool available = false;
    bool connected = false;
    std::string ssid;
    std::string ip;
};

inline constexpr Timezone kTimezones[] = {
    {"Etc/GMT+12", "UTC-12"}, {"Etc/GMT+11", "UTC-11"},
    {"Etc/GMT+10", "UTC-10"}, {"Etc/GMT+9", "UTC-9"},
    {"Etc/GMT+8", "UTC-8"},   {"Etc/GMT+7", "UTC-7"},
    {"Etc/GMT+6", "UTC-6"},   {"Etc/GMT+5", "UTC-5"},
    {"Etc/GMT+4", "UTC-4"},   {"Etc/GMT+3", "UTC-3"},
    {"Etc/GMT+2", "UTC-2"},   {"Etc/GMT+1", "UTC-1"},
    {"Etc/UTC", "UTC+0"},     {"Etc/GMT-1", "UTC+1"},
    {"Etc/GMT-2", "UTC+2"},   {"Etc/GMT-3", "UTC+3"},
    {"Etc/GMT-4", "UTC+4"},   {"Etc/GMT-5", "UTC+5"},
    {"Etc/GMT-6", "UTC+6"},   {"Etc/GMT-7", "UTC+7"},
    {"Etc/GMT-8", "UTC+8"},   {"Etc/GMT-9", "UTC+9"},
    {"Etc/GMT-10", "UTC+10"}, {"Etc/GMT-11", "UTC+11"},
};
inline constexpr int kTimezoneCount =
    static_cast<int>(sizeof(kTimezones) / sizeof(kTimezones[0]));
inline constexpr int kDefaultTimezoneIndex = 20;

struct WizardModel {
    Screen screen = Screen::Welcome;
    int timezone_index = kDefaultTimezoneIndex;
    int timezone_sel = kDefaultTimezoneIndex;
    std::string hostname = "CardputerZero";
    std::string username = "pi";
    std::string password = "pi";
    std::string confirm = "pi";
    int account_focus = 0;
    bool account_password_visible = false;
    bool account_warning_visible = false;
    std::string form_error;
    int network_focus = 0;
    bool network_skipped = false;
    bool use_ethernet = false;
    bool ethernet_dhcp = true;
    int ethernet_focus = 0;
    std::string ethernet_address = "192.168.1.100/24";
    std::string ethernet_gateway = "192.168.1.1";
    std::string ethernet_dns = "8.8.8.8";
    std::string ethernet_error;
    std::vector<WifiNetwork> wifi_list;
    int wifi_sel = 0;
    std::string wifi_ssid;
    std::string wifi_password;
    bool wifi_hidden = false;
    int wifi_focus = 0;
    bool wifi_manual = false;
    bool wifi_connected = false;
    bool wifi_connecting = false;
    bool wifi_connect_ready = false;
    bool wifi_connect_succeeded = false;
    bool wifi_password_visible = false;
    std::string wifi_connect_error;
    std::string wifi_ip;
    bool wifi_status_connected = false;
    std::string wifi_status_ssid;
    std::string wifi_status_ip;
    bool wifi_scanning = false;
    bool wifi_scan_ready = false;
    std::uint64_t wifi_scan_generation = 0;
    std::vector<WifiNetwork> wifi_scan_result;
    WifiConnectionStatus wifi_scan_status;
    std::string manual_date = "2026-06-16";
    std::string manual_time = "20:30";
    int time_focus = 0;
    bool time_warning_visible = false;
    std::string time_warning_message;
    bool ssh_enabled = true;
    int ssh_focus = 0;
    bool busy = false;
    bool worker_finished = false;
    bool succeeded = false;
    int exit_ticks = 0;
    std::string worker_message;
    std::mutex mutex;

    const Timezone &current_timezone() const;
};

bool validate_username(const std::string &name, std::string &error);
bool validate_password(const std::string &password, std::string &error);
bool validate_hostname(const std::string &name, std::string &error);
bool validate_wifi_ssid(const std::string &ssid, std::string &error);
bool valid_ipv4(const std::string &value);
bool valid_ipv4_cidr(const std::string &value);
std::string validate_ethernet_config(const WizardModel &model);

}  // namespace launch_wizard

#endif
