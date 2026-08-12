#include "cp0_lvgl_app.h"
#include "hal/hal_network.h"
#include "hal/hal_settings.h"
#include "hal_lvgl_bsp.h"
#include "../cp0_network_api_contract.hpp"
#include "../cp0_callback_result.hpp"
#include "../cp0_signal_registration.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <ifaddrs.h>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <net/if.h>
#include <string>
#include <utility>
#include <vector>

namespace {

static void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;
    if (!src)
        src = "";
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

class WifiSystem
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

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
        case cp0::network::ApiCommand::Status:
            result.complete(0, cp0::network::encode_status_payload(get_status()));
            break;
        case cp0::network::ApiCommand::Scan: {
            std::vector<cp0_wifi_ap_t> aps(static_cast<size_t>(request.scan_limit));
            int count = scan(aps.empty() ? nullptr : aps.data(), static_cast<int>(aps.size()));
            result.complete(count, cp0::network::encode_scan_payload(aps.data(), count));
            break;
        }
        case cp0::network::ApiCommand::Connect:
        case cp0::network::ApiCommand::ConnectHidden:
            result.complete(hal_wifi_connect(request.ssid.c_str(), request.password.c_str()), "");
            break;
        case cp0::network::ApiCommand::Disconnect:
        case cp0::network::ApiCommand::ProfileDisconnectActive:
            result.complete(hal_wifi_disconnect(), "");
            break;
        case cp0::network::ApiCommand::ProfileForget:
            result.complete(0, "");
            break;
        case cp0::network::ApiCommand::ProfileExists: {
            cp0_wifi_status_t st = get_status();
            result.complete(request.ssid == st.ssid ? 1 : 0, "");
            break;
        }
        case cp0::network::ApiCommand::RadioEnabled:
            result.complete(1, "");
            break;
        case cp0::network::ApiCommand::RadioSetEnabled:
            result.complete(0, "");
            break;
        }
        } catch (...) {
            result.complete(-1, "network api failure");
        }
    }

private:
    static cp0_wifi_status_t get_status()
    {
        hal_wifi_status_t hal = hal_wifi_get_status();
        cp0_wifi_status_t st{};
        st.connected = hal.connected;
        copy_cstr(st.ssid, sizeof(st.ssid), hal.ssid);
        copy_cstr(st.ip, sizeof(st.ip), hal.ip);
        st.signal = hal.signal;
        return st;
    }

    static int scan(cp0_wifi_ap_t *out, int max_aps)
    {
        if (!out || max_aps <= 0)
            return hal_wifi_scan(nullptr, 0);

        std::vector<hal_wifi_ap_t> hal_aps(static_cast<size_t>(max_aps));
        int count = hal_wifi_scan(hal_aps.data(), max_aps);
        count = std::min(count, max_aps);
        for (int i = 0; i < count; ++i) {
            copy_cstr(out[i].ssid, sizeof(out[i].ssid), hal_aps[static_cast<size_t>(i)].ssid);
            copy_cstr(out[i].security, sizeof(out[i].security), hal_aps[static_cast<size_t>(i)].security);
            out[i].signal = hal_aps[static_cast<size_t>(i)].signal;
            out[i].in_use = hal_aps[static_cast<size_t>(i)].in_use;
        }
        return count;
    }

};

} // namespace

namespace {

using WifiRegistration = cp0::SignalRegistration<decltype(cp0_signal_wifi_api)>;

WifiRegistration &wifi_registration()
{
    static WifiRegistration registration;
    return registration;
}

} // namespace

extern "C" int cp0_network_list(cp0_netif_info_t *entries, int max_entries, int *out_count)
{
    if (!out_count)
        return -1;
    *out_count = 0;

    if (!entries || max_entries <= 0)
        return hal_network_list(nullptr, 0, out_count);

    std::vector<hal_netif_info_t> hal_entries(static_cast<size_t>(max_entries));
    int ret = hal_network_list(hal_entries.data(), max_entries, out_count);
    int count = std::min(*out_count, max_entries);
    for (int i = 0; i < count; ++i) {
        copy_cstr(entries[i].iface, sizeof(entries[i].iface), hal_entries[static_cast<size_t>(i)].iface);
        copy_cstr(entries[i].ipv4, sizeof(entries[i].ipv4), hal_entries[static_cast<size_t>(i)].ipv4);
        copy_cstr(entries[i].netmask, sizeof(entries[i].netmask), hal_entries[static_cast<size_t>(i)].netmask);
        entries[i].is_up = hal_entries[static_cast<size_t>(i)].is_up;
    }
    *out_count = count;
    return ret;
}

extern "C" void init_wifi(void)
{
    static std::once_flag initialized;
    std::call_once(initialized, []() {
        auto wifi = std::make_shared<WifiSystem>();
        wifi_registration().replace(
            cp0_signal_wifi_api,
            [wifi](std::list<std::string> arg,
                   std::function<void(int, std::string)> callback) {
                wifi->api_call(std::move(arg), std::move(callback));
            });
    });
}

extern "C" void deinit_wifi(void)
{
    wifi_registration().reset();
}

extern "C" int hal_network_list(hal_netif_info_t *entries, int max_entries, int *out_count)
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
        if (std::strcmp(ifa->ifa_name, "lo") == 0 || std::strcmp(ifa->ifa_name, "lo0") == 0)
            continue;
        if (*out_count >= max_entries)
            break;

        hal_netif_info_t *entry = &entries[*out_count];
        copy_cstr(entry->iface, sizeof(entry->iface), ifa->ifa_name);
        auto *sa = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
        inet_ntop(AF_INET, &sa->sin_addr, entry->ipv4, sizeof(entry->ipv4));
        if (ifa->ifa_netmask) {
            auto *nm = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_netmask);
            inet_ntop(AF_INET, &nm->sin_addr, entry->netmask, sizeof(entry->netmask));
        } else {
            copy_cstr(entry->netmask, sizeof(entry->netmask), "N/A");
        }
        entry->is_up = (ifa->ifa_flags & IFF_UP) ? 1 : 0;
        ++(*out_count);
    }
    freeifaddrs(ifap);
    return 0;
}
