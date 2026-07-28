#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "../cp0_app_internal_utils.h"
#include "../cp0_callback_contract.hpp"
#include "../cp0_osinfo_codec.hpp"
#include "../cp0_osinfo_contract.hpp"
#include "../cp0_signal_registration.hpp"
#include "../cp0_sync_signal.hpp"
#include "../cp0_update_job.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <ifaddrs.h>
#include <list>
#include <iterator>
#include <memory>
#include <random>
#include <net/if.h>
#include <pwd.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>

namespace {

static time_t g_sdl_time_offset = 0;

static time_t parse_timestamp(const char *timestamp, bool *ok)
{
    if (ok)
        *ok = false;
    if (!timestamp || !timestamp[0])
        return 0;

    std::tm tm{};
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(timestamp, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return 0;

    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;

    time_t parsed = std::mktime(&tm);
    if (parsed == static_cast<time_t>(-1))
        return 0;
    if (ok)
        *ok = true;
    return parsed;
}

class SdlOsInfoSystem {
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

    SdlOsInfoSystem()
    {
        operations_.read_eth_info = [](bool, cp0_eth_info_t *info) {
            return network_default_info_read(info);
        };
        operations_.list_networks = [](cp0_netif_info_t *entries, int capacity, int *count) {
            return cp0_network_list(entries, capacity, count);
        };
        operations_.read_account_info = account_info_read;
        operations_.set_time = [](const std::string &timestamp) { return time_set(timestamp.c_str()); };
        operations_.read_local_time = read_local_time;
        operations_.random_u32 = random_u32;
        operations_.get_ntp = [] { return 0; };
        operations_.set_ntp = [](bool) { return 0; };
        operations_.apt_update_background = apt_update_background;
        operations_.update_launcher_background = update_launcher_background;
    }

    void api_call(arg_t arg, callback_t callback)
    {
        const std::string command = arg.empty() ? std::string() : arg.front();
        if (command == "AptUpdateStart" || command == "UpdateLauncherStart") {
            const std::string id = jobs_.start([] {
                return cp0::update::Result{-ENOTSUP, "unsupported"};
            });
            cp0::callback::invoke(callback, 0, id);
            return;
        }
        if (command == "UpdateJobStatus") {
            const auto id = arg.size() > 1 ? *std::next(arg.begin()) : std::string();
            std::string status;
            cp0::callback::invoke(callback, jobs_.status(id, status) ? 0 : -1, status);
            return;
        }
        if (command == "UpdateJobCancel") {
            const auto id = arg.size() > 1 ? *std::next(arg.begin()) : std::string();
            cp0::callback::invoke(callback, jobs_.cancel(id) ? 0 : -1, {});
            return;
        }
        if (command == "UpdateLauncherState") {
            cp0::callback::invoke(callback, -ENOTSUP, {});
            return;
        }
        const cp0::osinfo::Result result = cp0::osinfo::dispatch(arg, operations_);
        cp0::callback::invoke(callback, result.code, result.payload);
    }

    static int api_simple(const arg_t &arg, std::string *out = nullptr)
    {
        int result = -1;
        if (out) out->clear();
        cp0::signal::invoke_noexcept([&] { cp0_signal_osinfo_api(arg, [&](int code, std::string data) {
            result = code;
            if (out)
                *out = std::move(data);
        }); });
        return result;
    }

private:
    cp0::osinfo::Operations operations_;
    cp0::update::Jobs jobs_;

    static uint32_t random_u32() noexcept
    {
        try {
            std::random_device source;
            return static_cast<uint32_t>(source());
        } catch (...) {
            const auto seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            static thread_local std::mt19937 fallback(seed);
            return fallback();
        }
    }

    static int read_local_time(std::tm *value)
    {
        if (!value)
            return -1;
        const std::time_t now = std::time(nullptr);
#ifdef _WIN32
        return localtime_s(value, &now) == 0 ? 0 : -1;
#else
        return localtime_r(&now, value) ? 0 : (errno ? -errno : -1);
#endif
    }

    static std::string default_iface_from_route()
    {
        std::ifstream route("/proc/net/route");
        std::string line;
        std::getline(route, line);
        while (std::getline(route, line)) {
            std::istringstream iss(line);
            std::string iface;
            unsigned long dest = 0;
            if (iss >> iface >> std::hex >> dest && dest == 0)
                return iface;
        }
        return "";
    }

    static std::string default_gateway_from_route(const std::string &iface)
    {
        std::ifstream route("/proc/net/route");
        std::string line;
        std::getline(route, line);
        while (std::getline(route, line)) {
            std::istringstream iss(line);
            std::string row_iface;
            unsigned long dest = 0;
            unsigned long gateway = 0;
            if (!(iss >> row_iface >> std::hex >> dest >> gateway))
                continue;
            if (dest != 0 || (!iface.empty() && row_iface != iface))
                continue;
            struct in_addr addr;
            addr.s_addr = static_cast<in_addr_t>(gateway);
            char buf[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &addr, buf, sizeof(buf)))
                return buf;
        }
        return "N/A";
    }

    static std::string mac_for_iface(const std::string &iface)
    {
        if (iface.empty())
            return "N/A";
        std::ifstream file("/sys/class/net/" + iface + "/address");
        std::string mac;
        if (std::getline(file, mac) && !mac.empty())
            return mac;
        return "N/A";
    }

    static int network_default_info_read(cp0_eth_info_t *info)
    {
        cp0::osinfo::clear_eth_info(info);
        if (!info)
            return -1;

        std::string iface = default_iface_from_route();
        struct ifaddrs *ifap = nullptr;
        if (getifaddrs(&ifap) == 0) {
            for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                    continue;
                if (std::strcmp(ifa->ifa_name, "lo") == 0 || std::strcmp(ifa->ifa_name, "lo0") == 0)
                    continue;
                if (!iface.empty() && iface != ifa->ifa_name)
                    continue;
                iface = ifa->ifa_name;
                struct sockaddr_in *sa = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
                char ip[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip)))
                    cp0_copy_cstr(info->ipv4, sizeof(info->ipv4), ip);
                break;
            }
            freeifaddrs(ifap);
        }

        cp0_copy_string(info->gateway, sizeof(info->gateway), default_gateway_from_route(iface));
        cp0_copy_string(info->mac, sizeof(info->mac), mac_for_iface(iface));
        return 0;
    }

    static int account_info_read(cp0_account_info_t *info)
    {
        if (!info)
            return -1;
        std::memset(info, 0, sizeof(*info));
        const char *user = getlogin();
        if (!user || !user[0]) {
            struct passwd *pw = getpwuid(getuid());
            user = pw ? pw->pw_name : nullptr;
        }
        cp0_copy_cstr(info->user, sizeof(info->user), user && user[0] ? user : "N/A");
        char host[sizeof(info->hostname)] = {};
        if (gethostname(host, sizeof(host) - 1) == 0 && host[0])
            cp0_copy_cstr(info->hostname, sizeof(info->hostname), host);
        else
            cp0_copy_cstr(info->hostname, sizeof(info->hostname), "SDL");
        return 0;
    }

    static int time_set(const char *timestamp)
    {
        bool ok = false;
        const time_t target = parse_timestamp(timestamp, &ok);
        if (!ok)
            return -1;
        g_sdl_time_offset = target - std::time(nullptr);
        return 0;
    }

    static int apt_update_background()
    {
        return 0;
    }

    static int update_launcher_background()
    {
        return 0;
    }

public:
    static int api_eth_info(const char *command, cp0_eth_info_t *info)
    {
        if (!info)
            return -1;
        std::string data;
        int ret = api_simple({command}, &data);
        if (ret == 0 && !cp0::osinfo::decode_eth_info_strict(data, info))
            ret = -1;
        return ret;
    }

    static int api_account_info(cp0_account_info_t *info)
    {
        if (!info)
            return -1;
        std::string data;
        int ret = api_simple({"AccountInfoRead"}, &data);
        if (ret == 0 && !cp0::osinfo::decode_account_info_strict(data, info))
            ret = -1;
        return ret;
    }
};

} // namespace

extern "C" time_t cp0_sdl_time_now(void)
{
    return std::time(nullptr) + g_sdl_time_offset;
}

extern "C" void init_osinfo(void)
{
    static cp0::SignalRegistration<decltype(cp0_signal_osinfo_api)> registration;
    auto osinfo = std::make_shared<SdlOsInfoSystem>();
    registration.replace(cp0_signal_osinfo_api, [osinfo](std::list<std::string> arg, std::function<void(int, std::string)> callback) {
        osinfo->api_call(std::move(arg), std::move(callback));
    });
}

extern "C" int cp0_network_default_info_read(cp0_eth_info_t *info)
{
    return SdlOsInfoSystem::api_eth_info("NetworkDefaultInfoRead", info);
}

extern "C" int cp0_eth_info_read(cp0_eth_info_t *info)
{
    return SdlOsInfoSystem::api_eth_info("EthInfoRead", info);
}

extern "C" int cp0_account_info_read(cp0_account_info_t *info)
{
    return SdlOsInfoSystem::api_account_info(info);
}

extern "C" int cp0_time_set(const char *timestamp)
{
    return SdlOsInfoSystem::api_simple({"TimeSet", timestamp ? timestamp : ""});
}

extern "C" int cp0_time_ntp_get(void)
{
    return SdlOsInfoSystem::api_simple({"NtpGet"});
}

extern "C" int cp0_time_ntp_set(int enable)
{
    return SdlOsInfoSystem::api_simple({"NtpSet", enable ? "1" : "0"});
}

extern "C" int cp0_system_apt_update_background(void)
{
    return SdlOsInfoSystem::api_simple({"AptUpdateBackground"});
}

extern "C" int cp0_system_update_launcher_background(void)
{
    return SdlOsInfoSystem::api_simple({"UpdateLauncherBackground"});
}
