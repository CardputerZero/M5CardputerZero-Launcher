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
    const char *label;
};

struct WifiNetwork {
    std::string ssid;
    int signal = 0;
    std::string security;
};

enum class WifiSecurity {
    Open,
    Personal,
    Enterprise,
    Unsupported,
    Unknown,
};

enum class WifiScanDecision {
    Retry,
    Results,
    Empty,
    Error,
};

inline constexpr int kWifiMaxAutomaticScans = 4;

class WifiScanRetryPolicy {
public:
    WifiScanDecision observe(int error, std::size_t network_count);
    void reset() noexcept { attempts_ = 0; }
    int attempts() const noexcept { return attempts_; }

private:
    int attempts_ = 0;
};

struct WifiConnectionStatus {
    bool available = false;
    bool connected = false;
    std::string ssid;
    std::string ip;
};

inline constexpr Timezone kTimezones[] = {
    {"Pacific/Pago_Pago",       "UTC-11:00"},
    {"Pacific/Honolulu",        "UTC-10:00"},
    {"America/Anchorage",       "UTC-09:00"},
    {"America/Los_Angeles",     "UTC-08:00"},
    {"America/Denver",          "UTC-07:00"},
    {"America/Chicago",         "UTC-06:00"},
    {"America/New_York",        "UTC-05:00"},
    {"America/Halifax",         "UTC-04:00"},
    {"America/St_Johns",        "UTC-03:30"},
    {"America/Sao_Paulo",       "UTC-03:00"},
    {"Atlantic/South_Georgia",  "UTC-02:00"},
    {"Atlantic/Azores",         "UTC-01:00"},
    {"Europe/London",           "UTC+00:00"},
    {"Europe/Paris",            "UTC+01:00"},
    {"Europe/Helsinki",         "UTC+02:00"},
    {"Europe/Istanbul",         "UTC+03:00"},
    {"Asia/Tehran",             "UTC+03:30"},
    {"Asia/Dubai",              "UTC+04:00"},
    {"Asia/Kabul",              "UTC+04:30"},
    {"Asia/Karachi",            "UTC+05:00"},
    {"Asia/Kolkata",            "UTC+05:30"},
    {"Asia/Kathmandu",          "UTC+05:45"},
    {"Asia/Dhaka",              "UTC+06:00"},
    {"Asia/Yangon",             "UTC+06:30"},
    {"Asia/Bangkok",            "UTC+07:00"},
    {"Asia/Shanghai",           "UTC+08:00"},
    {"Australia/Eucla",         "UTC+08:45"},
    {"Asia/Tokyo",              "UTC+09:00"},
    {"Australia/Adelaide",      "UTC+09:30"},
    {"Australia/Sydney",        "UTC+10:00"},
    {"Australia/Lord_Howe",     "UTC+10:30"},
    {"Pacific/Noumea",          "UTC+11:00"},
    {"Pacific/Auckland",        "UTC+12:00"},
    {"Pacific/Chatham",         "UTC+12:45"},
    {"Pacific/Apia",            "UTC+13:00"},
    {"Pacific/Kiritimati",      "UTC+14:00"},
};
inline constexpr int kTimezoneCount =
    static_cast<int>(sizeof(kTimezones) / sizeof(kTimezones[0]));
inline constexpr int kDefaultTimezoneIndex = 25;

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
    std::string wifi_security;
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
    bool wifi_scan_retrying = false;
    std::string wifi_scan_error;
    bool wifi_scan_ready = false;
    bool wifi_scan_final_empty = false;
    std::uint64_t wifi_scan_generation = 0;
    std::vector<WifiNetwork> wifi_scan_result;
    int wifi_scan_result_error = 0;
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
    int worker_step = 0;
    int worker_total = 8;
    std::mutex mutex;

    const Timezone &current_timezone() const;
};

bool validate_username(const std::string &name, std::string &error);
bool validate_password(const std::string &password, std::string &error);
bool validate_hostname(const std::string &name, std::string &error);
bool validate_wifi_ssid(const std::string &ssid, std::string &error);
WifiSecurity classify_wifi_security(const std::string &security);
bool validate_wifi_credentials(const std::string &ssid,
                               const std::string &security,
                               const std::string &password,
                               bool hidden,
                               std::string &error);
bool valid_ipv4(const std::string &value);
bool valid_ipv4_cidr(const std::string &value);
std::string validate_ethernet_config(const WizardModel &model);

}  // namespace launch_wizard

#endif
