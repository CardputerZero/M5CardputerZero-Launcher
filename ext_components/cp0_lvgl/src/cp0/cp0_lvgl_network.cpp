#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "../cp0_app_internal_utils.h"
#include "../cp0_network_api_contract.hpp"
#include "../cp0_callback_result.hpp"
#include "../cp0_signal_registration.hpp"
#include "cp0_network_policy.hpp"
#include "cp0_process_commands.hpp"
#include "cp0_wifi_error_policy.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

int cp0_network_list(cp0_netif_info_t *entries, int max_entries, int *out_count)
{
    if (!out_count)
        return -1;
    *out_count = 0;

    if (!entries || max_entries <= 0)
        return 0;

    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) != 0)
        return -1;

    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (std::strcmp(ifa->ifa_name, "lo") == 0)
            continue;
        if (*out_count >= max_entries)
            break;

        cp0_netif_info_t *e = &entries[*out_count];
        cp0_copy_string(e->iface, sizeof(e->iface), ifa->ifa_name);
        struct sockaddr_in *sa = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
        inet_ntop(AF_INET, &sa->sin_addr, e->ipv4, sizeof(e->ipv4));
        if (ifa->ifa_netmask) {
            struct sockaddr_in *nm = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_netmask);
            inet_ntop(AF_INET, &nm->sin_addr, e->netmask, sizeof(e->netmask));
        } else {
            cp0_copy_string(e->netmask, sizeof(e->netmask), "N/A");
        }
        e->is_up = (ifa->ifa_flags & IFF_UP) ? 1 : 0;
        (*out_count)++;
    }
    freeifaddrs(ifap);
    return 0;
}

class WifiSystem
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

    WifiSystem()
    {
        update_status_cache();
        worker_ = std::thread([this]() { poll_loop(); });
    }

    ~WifiSystem() { stop(); }

    void stop() noexcept
    {
        running_.store(false);
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void api_call(arg_t arg, callback_t callback)
    {
        cp0::CallbackResult result(std::move(callback));
        try {
        cp0::network::ApiRequest request;
        if (!cp0::network::parse_api_request(arg, request)) {
            result.complete(-1, cp0::network::invalid_api_request_message());
            return;
        }
        switch (request.command) {
        case cp0::network::ApiCommand::Status: {
            cp0_wifi_status_t st = get_status();
            result.complete(0, cp0::network::encode_status_payload(st));
            break;
        }
        case cp0::network::ApiCommand::Scan: {
            std::vector<cp0_wifi_ap_t> aps(static_cast<std::size_t>(request.scan_limit));
            int count = scan(aps.empty() ? nullptr : aps.data(), static_cast<int>(aps.size()));
            result.complete(count, count < 0 ? std::string() :
                cp0::network::encode_scan_payload(aps.data(), count));
            break;
        }
        case cp0::network::ApiCommand::Connect:
            result.complete(
                connect(request.ssid.c_str(), request.password.empty() ? nullptr : request.password.c_str()), "");
            break;
        case cp0::network::ApiCommand::ConnectHidden:
            result.complete(connect(request.ssid.c_str(),
                request.password.empty() ? nullptr : request.password.c_str(), true), "");
            break;
        case cp0::network::ApiCommand::Disconnect:
            result.complete(disconnect(), "");
            break;
        case cp0::network::ApiCommand::ProfileForget:
            result.complete(profile_forget(request.ssid.c_str()), "");
            break;
        case cp0::network::ApiCommand::ProfileExists:
            result.complete(profile_exists(request.ssid.c_str()), "");
            break;
        case cp0::network::ApiCommand::ProfileDisconnectActive:
            result.complete(profile_disconnect_active(), "");
            break;
        case cp0::network::ApiCommand::RadioEnabled:
            result.complete(radio_enabled(), "");
            break;
        case cp0::network::ApiCommand::RadioSetEnabled:
            result.complete(radio_set_enabled(request.radio_enabled), "");
            break;
        }
        } catch (...) {
            result.complete(-1, "network api failure");
        }
    }

    cp0_wifi_status_t get_status()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_;
    }

    int scan(cp0_wifi_ap_t *out, int max_aps)
    {
        constexpr int kScanCommandTimeoutMs = 8000;
        std::string command_output;
        int command_result = cp0_process_commands::capture_argv_with_timeout(
            {"nmcli", "dev", "wifi", "rescan"}, command_output,
            kScanCommandTimeoutMs);
        if (command_result == -ETIMEDOUT) return CP0_WIFI_ERROR_TIMEOUT;
        if (command_result != 0) return cp0::wifi::classify_command_failure(command_output);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::string output;
        command_result = cp0_process_commands::capture_argv_with_timeout(
            {"nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY,IN-USE",
             "dev", "wifi", "list"}, output, kScanCommandTimeoutMs);
        if (command_result == -ETIMEDOUT) return CP0_WIFI_ERROR_TIMEOUT;
        if (command_result != 0) return cp0::wifi::classify_command_failure(output);

        std::unordered_set<std::string> saved_profiles;
        std::string profiles_output;
        if (cp0_process_commands::capture_argv_with_timeout(
                {"nmcli", "-t", "--escape", "no", "-f", "UUID,TYPE,NAME", "con", "show"},
                profiles_output, kScanCommandTimeoutMs) == 0) {
            for (const auto &profile : cp0::network::parse_connection_profiles(profiles_output)) {
                if (profile.type == "802-11-wireless") saved_profiles.insert(profile.name);
            }
        }

        std::vector<cp0_wifi_ap_t> aps;
        for (const auto &parsed : cp0::network::parse_scan_output(output)) {
            cp0_wifi_ap_t ap{};
            cp0_copy_string(ap.ssid, sizeof(ap.ssid), parsed.ssid);
            ap.signal = parsed.signal;
            cp0_copy_string(ap.security, sizeof(ap.security), parsed.security);
            ap.in_use = parsed.in_use ? 1 : 0;
            ap.saved = saved_profiles.count(parsed.ssid) != 0 ? 1 : 0;
            aps.push_back(ap);
        }

        const int count = static_cast<int>(aps.size());
        if (out && max_aps > 0) {
            const int copy_count = std::min(count, max_aps);
            for (int i = 0; i < copy_count; ++i)
                out[i] = aps[static_cast<size_t>(i)];
            return copy_count;
        }
        return count;
    }

    int connect(const char *ssid, const char *password, bool hidden = false)
    {
        if (!ssid || !ssid[0])
            return -1;

        update_status_cache();
        const cp0_wifi_status_t current_status = get_status();
        if (current_status.connected && std::string(current_status.ssid) == ssid)
            return 0;

        constexpr const char *kActivationTimeoutSeconds = "20";
        const bool with_password = password && password[0];
        std::string output;
        int command_result = -1;
        auto run_connect = [&](std::vector<std::string> args) {
            return cp0_process_commands::capture_argv_with_timeout(
                args, output, 25000);
        };
        if (with_password && hidden) {
            command_result = run_connect({"nmcli", "--wait", kActivationTimeoutSeconds,
                "dev", "wifi", "connect", ssid, "password", password, "hidden", "yes"});
        } else if (with_password) {
            command_result = run_connect({"nmcli", "--wait", kActivationTimeoutSeconds,
                "dev", "wifi", "connect", ssid, "password", password});
        } else if (hidden) {
            command_result = run_connect({"nmcli", "--wait", kActivationTimeoutSeconds,
                "dev", "wifi", "connect", ssid, "hidden", "yes"});
        } else {
            command_result = run_connect({"nmcli", "--wait", kActivationTimeoutSeconds,
                "con", "up", "id", ssid});
            if (command_result != 0) {
                command_result = run_connect({"nmcli", "--wait", kActivationTimeoutSeconds,
                    "dev", "wifi", "connect", ssid});
            }
        }

        update_status_cache();
        const cp0_wifi_status_t status = get_status();
        if (status.connected && std::string(status.ssid) == ssid) {
            return 0;
        }

        // Failed. When the user just entered a password, nmcli may have saved a
        // profile with that wrong password (named after the SSID). Delete it so the
        // password is never persisted and the next attempt must re-enter it (#69).
        if (with_password) {
            profile_forget(ssid);
        }
        if (command_result == -ETIMEDOUT) return CP0_WIFI_ERROR_TIMEOUT;
        return cp0::wifi::classify_command_failure(output);
    }

    int disconnect()
    {
        int ret = profile_disconnect_active();
        update_status_cache();
        return ret;
    }

    int profile_forget(const char *ssid)
    {
        if (!ssid || !ssid[0])
            return -1;
        std::string profiles_output;
        const int list_result = cp0_process_commands::capture_argv_with_timeout(
            {"nmcli", "-t", "--escape", "no", "-f", "UUID,TYPE,NAME", "con", "show"},
            profiles_output, 5000);
        if (list_result != 0) return list_result;

        int deleted = 0;
        std::string output;
        for (const auto &profile : cp0::network::parse_connection_profiles(profiles_output)) {
            if (profile.type != "802-11-wireless" || profile.name != ssid) continue;
            const int delete_result = cp0_process_commands::capture_argv_with_timeout(
                {"nmcli", "con", "delete", "uuid", profile.uuid}, output, 5000);
            if (delete_result != 0) return delete_result;
            ++deleted;
        }
        return deleted > 0 ? 0 : CP0_WIFI_ERROR_NOT_FOUND;
    }

    int profile_exists(const char *ssid)
    {
        if (!ssid || !ssid[0])
            return 0;
        std::string output;
        if (cp0_process_commands::capture_argv_with_timeout(
                {"nmcli", "-t", "--escape", "no", "-f", "UUID,TYPE,NAME", "con", "show"},
                output, 5000) != 0)
            return 0;
        for (const auto &profile : cp0::network::parse_connection_profiles(output)) {
            if (profile.type == "802-11-wireless" && profile.name == ssid)
                return 1;
        }
        return 0;
    }

    int profile_disconnect_active()
    {
        // `con show --active` can list Ethernet before Wi-Fi. Disconnect by
        // the active Wi-Fi device so forgetting a Wi-Fi profile cannot bring
        // down eth0 as a side effect.
        std::string device_output;
        if (cp0_process_commands::capture_argv_with_timeout(
                {"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION",
                 "dev", "status"}, device_output, 5000) != 0)
            return CP0_WIFI_ERROR_SERVICE;

        const std::string wifi_iface = cp0::network::parse_device_status(device_output).wifi_interface;
        if (wifi_iface.empty())
            return CP0_WIFI_ERROR_NOT_FOUND;

        std::string output;
        return cp0_process_commands::capture_argv_with_timeout(
            {"nmcli", "dev", "disconnect", "iface", wifi_iface}, output, 5000);
    }

    int radio_enabled()
    {
        std::string output;
        if (cp0_process_commands::capture_argv_with_timeout(
                {"nmcli", "radio", "wifi"}, output, 5000) != 0)
            return 0;
        return cp0::network::parse_radio_enabled(output) ? 1 : 0;
    }

    int radio_set_enabled(bool enabled)
    {
        std::string output;
        int ret = cp0_process_commands::capture_argv_with_timeout(
            {"nmcli", "radio", "wifi", enabled ? "on" : "off"}, output, 5000);
        update_status_cache();
        return ret;
    }

private:
    std::mutex mutex_;
    cp0_wifi_status_t cache_{};
    std::thread worker_;
    std::atomic<bool> running_{true};
    std::condition_variable wake_;
    std::mutex wake_mutex_;

    void poll_loop()
    {
        while (running_.load()) {
            update_status_cache();
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_.wait_for(lock, std::chrono::seconds(3),
                           [this] { return !running_.load(); });
        }
    }

    void update_status_cache()
    {
        cp0_wifi_status_t st{};
        read_status(st);
        std::lock_guard<std::mutex> lock(mutex_);
        cache_ = st;
    }

    static void read_status(cp0_wifi_status_t &st)
    {
        std::string output;
        std::string wifi_iface;
        // 用 DEVICE,TYPE,STATE,CONNECTION 四列判断：只要 wifi 设备的 STATE 以 "connected"
        // 开头就算已连接，避免插拔网线后 wlan0 变成 "connected (externally)" 且 CONNECTION
        // 显示为 "--" 时被误判为未连接（#37）。
        if (cp0_process_commands::capture_argv_with_timeout(
                {"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "dev", "status"},
                output, 2000) == 0) {
            const auto parsed = cp0::network::parse_device_status(output);
            st.connected = parsed.connected ? 1 : 0;
            st.ethernet = parsed.ethernet ? 1 : 0;
            wifi_iface = parsed.wifi_interface;
            cp0_copy_string(st.ssid, sizeof(st.ssid), parsed.ssid);
        }

        if (!st.connected)
            return;

        if (wifi_iface.empty())
            wifi_iface = "wlan0";

        output.clear();
        if (cp0_process_commands::capture_argv_with_timeout(
                {"nmcli", "-t", "-f", "IN-USE,SIGNAL,SSID", "dev", "wifi", "list",
                 "--rescan", "no"}, output, 2000) == 0) {
            cp0::network::Status parsed;
            parsed.ssid = st.ssid;
            cp0::network::apply_active_wifi(output, parsed);
            st.signal = parsed.signal;
            cp0_copy_string(st.ssid, sizeof(st.ssid), parsed.ssid);
        }

        output.clear();
        if (cp0_process_commands::capture_argv_with_timeout(
                {"ip", "-4", "-o", "addr", "show", wifi_iface}, output, 2000) == 0) {
            cp0_copy_string(st.ip, sizeof(st.ip), cp0::network::parse_ipv4_address(output));
        }
    }

    static std::string active_connection_name()
    {
        std::string output;
        if (cp0_process_commands::capture_argv_with_timeout(
                {"nmcli", "-t", "-f", "NAME", "con", "show", "--active"},
                output, 5000) != 0)
            return {};
        return cp0::network::first_active_connection(output);
    }
};

using WifiRegistration = cp0::SignalRegistration<decltype(cp0_signal_wifi_api)>;

struct WifiRuntime {
    std::mutex mutex;
    WifiRegistration registration;
    std::shared_ptr<WifiSystem> service;
};

WifiRuntime &wifi_runtime()
{
    static WifiRuntime runtime;
    return runtime;
}

extern "C" void init_wifi(void)
{
    static std::once_flag initialized;
    std::call_once(initialized, []() {
        auto wifi = std::make_shared<WifiSystem>();
        WifiRuntime &runtime = wifi_runtime();
        if (!runtime.registration.replace(
                cp0_signal_wifi_api,
                [wifi](std::list<std::string> arg,
                       std::function<void(int, std::string)> callback) {
                    wifi->api_call(std::move(arg), std::move(callback));
                }))
            return;
        std::lock_guard<std::mutex> lock(runtime.mutex);
        runtime.service = std::move(wifi);
    });
}

extern "C" void deinit_wifi(void)
{
    WifiRuntime &runtime = wifi_runtime();
    runtime.registration.reset();
    std::shared_ptr<WifiSystem> wifi;
    {
        std::lock_guard<std::mutex> lock(runtime.mutex);
        wifi = std::move(runtime.service);
    }
    if (wifi) wifi->stop();
}
