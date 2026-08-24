#include "nmtui.h"

#include "cp0_callback_result.hpp"
#include "cp0_lvgl_app.h"
#include "cp0_network_api_contract.hpp"
#include "cp0_signal_registration.hpp"
#include "hal_lvgl_bsp.h"

#include <NetworkManager.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kActivationTimeoutMs = 25000;
constexpr int kScanTimeoutMs = 8000;
constexpr int kRadioTimeoutMs = 5000;

void copy_string(char *destination, std::size_t capacity, const std::string &value)
{
    if (!destination || capacity == 0)
        return;
    const std::size_t count = std::min(capacity - 1, value.size());
    std::memcpy(destination, value.data(), count);
    destination[count] = '\0';
}

std::string error_message(const GError *error)
{
    return error && error->message ? error->message : std::string();
}

std::string ssid_from_bytes(GBytes *bytes)
{
    if (!bytes)
        return {};

    gsize length = 0;
    const auto *data = static_cast<const guint8 *>(g_bytes_get_data(bytes, &length));
    if (!data || length == 0)
        return {};

    char *utf8 = nm_utils_ssid_to_utf8(data, length);
    if (!utf8)
        return {};
    std::string result(utf8);
    g_free(utf8);
    return result;
}

std::string security_name(NMAccessPoint *access_point)
{
    if (!access_point)
        return "OPEN";

    const auto ap_flags = nm_access_point_get_flags(access_point);
    const auto wpa_flags = nm_access_point_get_wpa_flags(access_point);
    const auto rsn_flags = nm_access_point_get_rsn_flags(access_point);
    const auto all_security = static_cast<NM80211ApSecurityFlags>(wpa_flags | rsn_flags);

    if (all_security & NM_802_11_AP_SEC_KEY_MGMT_SAE)
        return "WPA3";
    if (all_security & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
        return "WPA-EAP";
    if (all_security & NM_802_11_AP_SEC_KEY_MGMT_PSK)
        return rsn_flags ? "WPA2" : "WPA";
    if (ap_flags & NM_802_11_AP_FLAGS_PRIVACY)
        return "WEP";
    return "OPEN";
}

bool has_wep_security(NMAccessPoint *access_point)
{
    if (!access_point)
        return false;
    const auto flags = static_cast<NM80211ApSecurityFlags>(
        nm_access_point_get_wpa_flags(access_point) |
        nm_access_point_get_rsn_flags(access_point));
    return (flags & (NM_802_11_AP_SEC_PAIR_WEP40 |
                     NM_802_11_AP_SEC_PAIR_WEP104 |
                     NM_802_11_AP_SEC_GROUP_WEP40 |
                     NM_802_11_AP_SEC_GROUP_WEP104)) != 0;
}

int classify_error_text(const std::string &message)
{
    std::string lower;
    lower.reserve(message.size());
    for (const char character : message)
        lower += static_cast<char>(g_ascii_tolower(character));

    if (lower.find("timeout") != std::string::npos ||
        lower.find("timed out") != std::string::npos)
        return CP0_WIFI_ERROR_TIMEOUT;
    if (lower.find("secret") != std::string::npos ||
        lower.find("password") != std::string::npos ||
        lower.find("authentication") != std::string::npos ||
        lower.find("auth") != std::string::npos ||
        lower.find("key") != std::string::npos)
        return CP0_WIFI_ERROR_AUTH;
    if (lower.find("not found") != std::string::npos ||
        lower.find("no network") != std::string::npos ||
        lower.find("unavailable") != std::string::npos)
        return CP0_WIFI_ERROR_NOT_FOUND;
    if (lower.find("dhcp") != std::string::npos ||
        lower.find("ip config") != std::string::npos ||
        lower.find("ip configuration") != std::string::npos)
        return CP0_WIFI_ERROR_IP_CONFIG;
    if (lower.find("networkmanager") != std::string::npos ||
        lower.find("manager") != std::string::npos ||
        lower.find("service") != std::string::npos)
        return CP0_WIFI_ERROR_SERVICE;
    return CP0_WIFI_ERROR_INVALID;
}

struct ActivationState {
    std::mutex mutex;
    bool done = false;
    bool discard = false;
    NMActiveConnection *active = nullptr;
    GError *error = nullptr;
};

struct ActivationStateHolder {
    std::shared_ptr<ActivationState> state;
    NMClient *client = nullptr;
};

void release_activation_holder(ActivationStateHolder *holder)
{
    if (!holder)
        return;
    if (holder->client)
        g_object_unref(holder->client);
    delete holder;
}

void complete_activation(ActivationStateHolder *holder, NMActiveConnection *active,
                         GError *error)
{
    const auto state = holder->state;
    bool discard = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        discard = state->discard;
        if (!discard) {
            state->active = active;
            state->error = error;
            active = nullptr;
            error = nullptr;
        }
        state->done = true;
    }
    if (active)
        g_object_unref(active);
    if (error)
        g_error_free(error);
    release_activation_holder(holder);
}

void add_activation_callback(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    auto *holder = static_cast<ActivationStateHolder *>(user_data);
    GError *error = nullptr;
    NMActiveConnection *active =
        nm_client_add_and_activate_connection_finish(NM_CLIENT(source_object), result, &error);
    complete_activation(holder, active, error);
}

void existing_activation_callback(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    auto *holder = static_cast<ActivationStateHolder *>(user_data);
    GError *error = nullptr;
    NMActiveConnection *active =
        nm_client_activate_connection_finish(NM_CLIENT(source_object), result, &error);
    complete_activation(holder, active, error);
}

struct ActivationOutcome {
    int code = CP0_WIFI_ERROR_INVALID;
    std::string message;
    NMActiveConnection *active = nullptr;
};

class NmtuiWifiAdapter
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

    ~NmtuiWifiAdapter()
    {
        if (client_)
            g_object_unref(client_);
    }

    void api_call(arg_t args, callback_t callback)
    {
        cp0::CallbackResult result(std::move(callback));
        int code = CP0_WIFI_ERROR_INVALID;
        std::string data;
        try {
            cp0::network::ApiRequest request;
            if (!cp0::network::parse_api_request(args, request)) {
                result.complete(-1, cp0::network::invalid_api_request_message());
                return;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!ensure_client()) {
                    code = CP0_WIFI_ERROR_SERVICE;
                    data = last_error_;
                } else {
                    dispatch_context();
                    switch (request.command) {
                    case cp0::network::ApiCommand::Status: {
                        cp0_wifi_status_t status{};
                        code = read_status(status) ? 0 : CP0_WIFI_ERROR_SERVICE;
                        data = code == 0 ? cp0::network::encode_status_payload(status) : last_error_;
                        break;
                    }
                    case cp0::network::ApiCommand::Scan: {
                        std::vector<cp0_wifi_ap_t> access_points;
                        access_points.reserve(static_cast<std::size_t>(request.scan_limit));
                        code = scan(access_points);
                        if (code >= 0)
                            data = cp0::network::encode_scan_payload(
                                access_points.empty() ? nullptr : access_points.data(), code);
                        break;
                    }
                    case cp0::network::ApiCommand::Connect:
                        code = connect(request.ssid, request.password, false);
                        break;
                    case cp0::network::ApiCommand::ConnectHidden:
                        code = connect(request.ssid, request.password, true);
                        break;
                    case cp0::network::ApiCommand::Disconnect:
                    case cp0::network::ApiCommand::ProfileDisconnectActive:
                        code = disconnect_active();
                        break;
                    case cp0::network::ApiCommand::ProfileForget:
                        code = profile_forget(request.ssid);
                        break;
                    case cp0::network::ApiCommand::ProfileExists:
                        code = profile_exists(request.ssid);
                        break;
                    case cp0::network::ApiCommand::RadioEnabled:
                        code = nm_client_wireless_get_enabled(client_) ? 1 : 0;
                        break;
                    case cp0::network::ApiCommand::RadioSetEnabled:
                        code = set_radio_enabled(request.radio_enabled);
                        break;
                    }
                }
            }
        } catch (...) {
            code = CP0_WIFI_ERROR_INVALID;
            data = "network api failure";
        }
        result.complete(code, std::move(data));
    }

    bool available()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ensure_client();
    }

    std::string last_error() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_error_;
    }

private:
    NMClient *client_ = nullptr;
    std::string last_error_;
    mutable std::mutex mutex_;

    void set_error(const std::string &message)
    {
        last_error_ = message.empty() ? "NetworkManager operation failed" : message;
    }

    bool ensure_client()
    {
        if (client_) {
            dispatch_context();
            if (nm_client_get_nm_running(client_))
                return true;
            g_object_unref(client_);
            client_ = nullptr;
        }

        GError *error = nullptr;
        client_ = nm_client_new(nullptr, &error);
        if (client_) {
            dispatch_context();
            if (nm_client_get_nm_running(client_)) {
                if (error)
                    g_error_free(error);
                return true;
            }
            g_object_unref(client_);
            client_ = nullptr;
            if (error)
                g_error_free(error);
            set_error("NetworkManager is not running");
            return false;
        }
        set_error(error_message(error));
        if (error)
            g_error_free(error);
        return false;
    }

    void dispatch_context()
    {
        if (!client_)
            return;
        GMainContext *context = nm_client_get_main_context(client_);
        if (!context)
            return;
        while (g_main_context_pending(context))
            g_main_context_iteration(context, FALSE);
    }

    static bool is_activated(NMDevice *device)
    {
        return device && nm_device_get_state(device) == NM_DEVICE_STATE_ACTIVATED;
    }

    NMDevice *wifi_device(bool require_activated = false) const
    {
        const GPtrArray *devices = nm_client_get_devices(client_);
        if (!devices)
            return nullptr;
        NMDevice *fallback = nullptr;
        for (guint index = 0; index < devices->len; ++index) {
            auto *device = static_cast<NMDevice *>(g_ptr_array_index(devices, index));
            if (!device || nm_device_get_device_type(device) != NM_DEVICE_TYPE_WIFI)
                continue;
            if (!fallback)
                fallback = device;
            if (is_activated(device) || !require_activated)
                return device;
        }
        return require_activated ? nullptr : fallback;
    }

    static std::string active_ssid(NMDeviceWifi *device)
    {
        if (!device)
            return {};
        NMAccessPoint *access_point = nm_device_wifi_get_active_access_point(device);
        std::string ssid = ssid_from_bytes(
            access_point ? nm_access_point_get_ssid(access_point) : nullptr);
        if (!ssid.empty())
            return ssid;

        NMActiveConnection *active = nm_device_get_active_connection(NM_DEVICE(device));
        NMRemoteConnection *connection =
            active ? nm_active_connection_get_connection(active) : nullptr;
        auto *setting = connection
            ? NM_SETTING_WIRELESS(nm_connection_get_setting(
                  NM_CONNECTION(connection), NM_TYPE_SETTING_WIRELESS))
            : nullptr;
        return setting ? ssid_from_bytes(nm_setting_wireless_get_ssid(setting)) : std::string();
    }

    static std::string ipv4_address(NMDevice *device)
    {
        NMIPConfig *config = device ? nm_device_get_ip4_config(device) : nullptr;
        GPtrArray *addresses = config ? nm_ip_config_get_addresses(config) : nullptr;
        if (!addresses)
            return {};
        for (guint index = 0; index < addresses->len; ++index) {
            auto *address = static_cast<NMIPAddress *>(g_ptr_array_index(addresses, index));
            const char *value = address ? nm_ip_address_get_address(address) : nullptr;
            if (value && value[0])
                return value;
        }
        return {};
    }

    bool read_status(cp0_wifi_status_t &status)
    {
        status = {};
        NMDevice *ip_device = nullptr;
        const GPtrArray *devices = nm_client_get_devices(client_);
        if (!devices)
            return false;

        for (guint index = 0; index < devices->len; ++index) {
            auto *device = static_cast<NMDevice *>(g_ptr_array_index(devices, index));
            if (!device || !is_activated(device))
                continue;
            const NMDeviceType type = nm_device_get_device_type(device);
            if (type == NM_DEVICE_TYPE_ETHERNET) {
                status.connected = 1;
                status.ethernet = 1;
                if (!ip_device)
                    ip_device = device;
            } else if (type == NM_DEVICE_TYPE_WIFI) {
                status.connected = 1;
                ip_device = device;
                auto *wifi = NM_DEVICE_WIFI(device);
                copy_string(status.ssid, sizeof(status.ssid), active_ssid(wifi));
                NMAccessPoint *access_point = nm_device_wifi_get_active_access_point(wifi);
                if (access_point)
                    status.signal = static_cast<int>(nm_access_point_get_strength(access_point));
            }
        }

        if (ip_device)
            copy_string(status.ip, sizeof(status.ip), ipv4_address(ip_device));
        return true;
    }

    std::string profile_id(NMRemoteConnection *connection) const
    {
        if (!connection)
            return {};
        auto *setting = NM_SETTING_CONNECTION(nm_connection_get_setting(
            NM_CONNECTION(connection), NM_TYPE_SETTING_CONNECTION));
        const char *id = setting ? nm_setting_connection_get_id(setting) : nullptr;
        return id ? id : std::string();
    }

    std::string profile_ssid(NMRemoteConnection *connection) const
    {
        if (!connection)
            return {};
        auto *setting = NM_SETTING_WIRELESS(nm_connection_get_setting(
            NM_CONNECTION(connection), NM_TYPE_SETTING_WIRELESS));
        return setting ? ssid_from_bytes(nm_setting_wireless_get_ssid(setting)) : std::string();
    }

    bool profile_matches(NMRemoteConnection *connection, const std::string &ssid) const
    {
        if (!connection)
            return false;
        auto *setting = NM_SETTING_CONNECTION(nm_connection_get_setting(
            NM_CONNECTION(connection), NM_TYPE_SETTING_CONNECTION));
        const char *type = setting ? nm_setting_connection_get_connection_type(setting) : nullptr;
        if (!type || std::strcmp(type, NM_SETTING_WIRELESS_SETTING_NAME) != 0)
            return false;
        return profile_id(connection) == ssid || profile_ssid(connection) == ssid;
    }

    NMRemoteConnection *find_profile(const std::string &ssid) const
    {
        const GPtrArray *connections = nm_client_get_connections(client_);
        if (!connections)
            return nullptr;
        for (guint index = 0; index < connections->len; ++index) {
            auto *connection = static_cast<NMRemoteConnection *>(
                g_ptr_array_index(connections, index));
            if (profile_matches(connection, ssid))
                return connection;
        }
        return nullptr;
    }

    NMAccessPoint *find_access_point(NMDeviceWifi *device, const std::string &ssid) const
    {
        if (!device)
            return nullptr;
        const GPtrArray *access_points = nm_device_wifi_get_access_points(device);
        if (!access_points)
            return nullptr;
        for (guint index = 0; index < access_points->len; ++index) {
            auto *access_point = static_cast<NMAccessPoint *>(
                g_ptr_array_index(access_points, index));
            if (ssid_from_bytes(access_point ? nm_access_point_get_ssid(access_point) : nullptr) ==
                ssid)
                return access_point;
        }
        return nullptr;
    }

    NMConnection *make_connection(const std::string &ssid, const std::string &password,
                                  bool hidden, NMAccessPoint *access_point) const
    {
        NMConnection *connection = nm_simple_connection_new();
        if (!connection)
            return nullptr;

        NMSetting *connection_setting = nm_setting_connection_new();
        char *uuid = nm_utils_uuid_generate();
        g_object_set(G_OBJECT(connection_setting),
                     NM_SETTING_CONNECTION_ID, ssid.c_str(),
                     NM_SETTING_CONNECTION_UUID, uuid,
                     NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
                     NM_SETTING_CONNECTION_AUTOCONNECT, TRUE,
                     nullptr);
        g_free(uuid);
        nm_connection_add_setting(connection, connection_setting);

        NMSetting *wireless_setting = nm_setting_wireless_new();
        GBytes *ssid_bytes = g_bytes_new(ssid.data(), ssid.size());
        g_object_set(G_OBJECT(wireless_setting),
                     NM_SETTING_WIRELESS_SSID, ssid_bytes,
                     NM_SETTING_WIRELESS_MODE, NM_SETTING_WIRELESS_MODE_INFRA,
                     NM_SETTING_WIRELESS_HIDDEN, hidden,
                     nullptr);
        g_bytes_unref(ssid_bytes);
        nm_connection_add_setting(connection, wireless_setting);

        if (!password.empty()) {
            NMSetting *security_setting = nm_setting_wireless_security_new();
            if (has_wep_security(access_point)) {
                g_object_set(G_OBJECT(security_setting),
                             NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "none",
                             NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open",
                             NM_SETTING_WIRELESS_SECURITY_WEP_KEY0, password.c_str(),
                             nullptr);
            } else {
                const auto flags = static_cast<NM80211ApSecurityFlags>(
                    access_point ? nm_access_point_get_wpa_flags(access_point) |
                                       nm_access_point_get_rsn_flags(access_point)
                                 : 0);
                const char *key_management =
                    (flags & NM_802_11_AP_SEC_KEY_MGMT_SAE) &&
                            !(flags & NM_802_11_AP_SEC_KEY_MGMT_PSK)
                        ? "sae"
                        : "wpa-psk";
                g_object_set(G_OBJECT(security_setting),
                             NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, key_management,
                             NM_SETTING_WIRELESS_SECURITY_PSK, password.c_str(),
                             nullptr);
            }
            nm_connection_add_setting(connection, security_setting);
        }
        return connection;
    }

    ActivationOutcome activate(NMConnection *connection, NMDevice *device,
                               const char *specific_object, bool add)
    {
        ActivationOutcome outcome;
        if (!connection || !device)
            return outcome;

        GMainContext *context = nm_client_get_main_context(client_);
        auto state = std::make_shared<ActivationState>();
        auto *holder = new ActivationStateHolder{state, NM_CLIENT(g_object_ref(client_))};
        GCancellable *cancellable = g_cancellable_new();
        if (add) {
            nm_client_add_and_activate_connection_async(
                client_, connection, device, specific_object, cancellable,
                add_activation_callback, holder);
        } else {
            nm_client_activate_connection_async(
                client_, connection, device, specific_object, cancellable,
                existing_activation_callback, holder);
        }

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kActivationTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->done)
                    break;
            }
            if (!context || !g_main_context_iteration(context, FALSE))
                g_usleep(10000);
        }

        NMActiveConnection *active = nullptr;
        GError *error = nullptr;
        GCancellable *timeout_cancellable = nullptr;
        bool timed_out = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->done) {
                state->discard = true;
                timeout_cancellable = G_CANCELLABLE(g_object_ref(cancellable));
                timed_out = true;
            } else {
                active = state->active;
                state->active = nullptr;
                error = state->error;
                state->error = nullptr;
            }
        }
        if (timed_out) {
            if (timeout_cancellable) {
                g_cancellable_cancel(timeout_cancellable);
                g_object_unref(timeout_cancellable);
            }
            g_object_unref(cancellable);
            outcome.code = CP0_WIFI_ERROR_TIMEOUT;
            outcome.message = "NetworkManager activation timed out";
            return outcome;
        }

        if (error) {
            outcome.message = error_message(error);
            outcome.code = classify_error_text(outcome.message);
            g_error_free(error);
            g_object_unref(cancellable);
            return outcome;
        }
        outcome.active = active;
        outcome.code = outcome.active ? 0 : CP0_WIFI_ERROR_INVALID;
        g_object_unref(cancellable);
        return outcome;
    }

    bool wait_until_activated(NMDevice *device, NMActiveConnection *active)
    {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kActivationTimeoutMs);
        GMainContext *context = nm_client_get_main_context(client_);
        while (std::chrono::steady_clock::now() < deadline) {
            dispatch_context();
            if (is_activated(device) &&
                (!active || nm_active_connection_get_state(active) ==
                                NM_ACTIVE_CONNECTION_STATE_ACTIVATED))
                return true;
            if (active && nm_active_connection_get_state(active) ==
                               NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
                return false;
            if (!context || !g_main_context_iteration(context, FALSE))
                g_usleep(100000);
        }
        return false;
    }

    int connect(const std::string &ssid, const std::string &password, bool hidden)
    {
        if (ssid.empty())
            return CP0_WIFI_ERROR_INVALID;
        if (!nm_client_wireless_get_enabled(client_))
            return CP0_WIFI_ERROR_RADIO_OFF;

        auto *device = wifi_device();
        if (!device)
            return CP0_WIFI_ERROR_NOT_FOUND;
        auto *wifi = NM_DEVICE_WIFI(device);
        auto *access_point = find_access_point(wifi, ssid);
        auto *profile = find_profile(ssid);
        std::string old_uuid;
        if (profile) {
            auto *setting = NM_SETTING_CONNECTION(nm_connection_get_setting(
                NM_CONNECTION(profile), NM_TYPE_SETTING_CONNECTION));
            const char *uuid = setting ? nm_setting_connection_get_uuid(setting) : nullptr;
            if (uuid)
                old_uuid = uuid;
        }

        NMConnection *connection = nullptr;
        bool add = false;
        if (password.empty() && profile) {
            connection = NM_CONNECTION(profile);
        } else {
            connection = make_connection(ssid, password, hidden, access_point);
            add = true;
        }
        if (!connection)
            return CP0_WIFI_ERROR_INVALID;

        const char *specific_object = access_point
            ? nm_object_get_path(NM_OBJECT(access_point))
            : nullptr;
        ActivationOutcome outcome = activate(connection, device, specific_object, add);
        if (add)
            g_object_unref(connection);
        if (outcome.code != 0) {
            if (outcome.active)
                g_object_unref(outcome.active);
            if (add)
                cleanup_failed_profile(ssid, old_uuid);
            set_error(outcome.message);
            return outcome.code;
        }

        const bool activated = wait_until_activated(device, outcome.active);
        if (outcome.active)
            g_object_unref(outcome.active);
        if (!activated) {
            if (add)
                cleanup_failed_profile(ssid, old_uuid);
            return CP0_WIFI_ERROR_TIMEOUT;
        }

        cp0_wifi_status_t status{};
        if (!read_status(status) || !status.connected || std::string(status.ssid) != ssid) {
            if (add)
                cleanup_failed_profile(ssid, old_uuid);
            return CP0_WIFI_ERROR_IP_CONFIG;
        }
        return 0;
    }

    void cleanup_failed_profile(const std::string &ssid, const std::string &old_uuid)
    {
        const GPtrArray *connections = nm_client_get_connections(client_);
        if (!connections)
            return;
        for (gint index = static_cast<gint>(connections->len) - 1; index >= 0; --index) {
            auto *connection = static_cast<NMRemoteConnection *>(
                g_ptr_array_index(connections, static_cast<guint>(index)));
            if (!profile_matches(connection, ssid))
                continue;
            auto *setting = NM_SETTING_CONNECTION(nm_connection_get_setting(
                NM_CONNECTION(connection), NM_TYPE_SETTING_CONNECTION));
            const char *uuid = setting ? nm_setting_connection_get_uuid(setting) : nullptr;
            if (old_uuid.empty() || !uuid || old_uuid != uuid) {
                GError *error = nullptr;
                G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                nm_remote_connection_delete(connection, nullptr, &error);
                G_GNUC_END_IGNORE_DEPRECATIONS
                if (error)
                    g_error_free(error);
            }
        }
    }

    int disconnect_active()
    {
        NMDevice *device = wifi_device(true);
        if (!device)
            return CP0_WIFI_ERROR_NOT_FOUND;
        GError *error = nullptr;
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        const gboolean success = nm_device_disconnect(device, nullptr, &error);
        G_GNUC_END_IGNORE_DEPRECATIONS
        if (!success) {
            const std::string message = error_message(error);
            if (error)
                g_error_free(error);
            set_error(message);
            return classify_error_text(message);
        }
        return 0;
    }

    int profile_forget(const std::string &ssid)
    {
        if (ssid.empty())
            return CP0_WIFI_ERROR_INVALID;
        const GPtrArray *connections = nm_client_get_connections(client_);
        if (!connections)
            return CP0_WIFI_ERROR_SERVICE;
        int deleted = 0;
        for (gint index = static_cast<gint>(connections->len) - 1; index >= 0; --index) {
            auto *connection = static_cast<NMRemoteConnection *>(
                g_ptr_array_index(connections, static_cast<guint>(index)));
            if (!profile_matches(connection, ssid))
                continue;
            GError *error = nullptr;
            gboolean deleted_ok = FALSE;
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            deleted_ok = nm_remote_connection_delete(connection, nullptr, &error);
            G_GNUC_END_IGNORE_DEPRECATIONS
            if (deleted_ok)
                ++deleted;
            else if (error) {
                const std::string message = error_message(error);
                set_error(message);
                g_error_free(error);
                return classify_error_text(message);
            }
        }
        return deleted ? 0 : CP0_WIFI_ERROR_NOT_FOUND;
    }

    int profile_exists(const std::string &ssid) const
    {
        return find_profile(ssid) ? 1 : 0;
    }

    int set_radio_enabled(bool enabled)
    {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        nm_client_wireless_set_enabled(client_, enabled);
        G_GNUC_END_IGNORE_DEPRECATIONS
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kRadioTimeoutMs);
        GMainContext *context = nm_client_get_main_context(client_);
        while (std::chrono::steady_clock::now() < deadline) {
            dispatch_context();
            if (nm_client_wireless_get_enabled(client_) == enabled)
                return 0;
            if (!context || !g_main_context_iteration(context, FALSE))
                g_usleep(50000);
        }
        set_error("NetworkManager did not change the Wi-Fi radio state");
        return CP0_WIFI_ERROR_TIMEOUT;
    }

    int scan(std::vector<cp0_wifi_ap_t> &output)
    {
        if (output.empty())
            return 0;
        if (!nm_client_wireless_get_enabled(client_))
            return CP0_WIFI_ERROR_RADIO_OFF;

        NMDevice *device = wifi_device();
        if (!device)
            return CP0_WIFI_ERROR_NOT_FOUND;
        auto *wifi = NM_DEVICE_WIFI(device);
        GError *error = nullptr;
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        const gboolean requested = nm_device_wifi_request_scan(wifi, nullptr, &error);
        G_GNUC_END_IGNORE_DEPRECATIONS
        if (!requested) {
            const std::string message = error_message(error);
            if (error)
                g_error_free(error);
            set_error(message);
            return classify_error_text(message);
        }

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kScanTimeoutMs);
        GMainContext *context = nm_client_get_main_context(client_);
        while (std::chrono::steady_clock::now() < deadline) {
            dispatch_context();
            const GPtrArray *access_points = nm_device_wifi_get_access_points(wifi);
            if (access_points && access_points->len > 0)
                break;
            if (!context || !g_main_context_iteration(context, FALSE))
                g_usleep(100000);
        }

        const GPtrArray *access_points = nm_device_wifi_get_access_points(wifi);
        if (!access_points)
            return CP0_WIFI_ERROR_SERVICE;

        std::unordered_map<std::string, std::size_t> by_ssid;
        const std::string active = active_ssid(wifi);
        for (guint index = 0; index < access_points->len; ++index) {
            auto *access_point = static_cast<NMAccessPoint *>(
                g_ptr_array_index(access_points, index));
            const std::string ssid = ssid_from_bytes(
                access_point ? nm_access_point_get_ssid(access_point) : nullptr);
            if (ssid.empty())
                continue;

            cp0_wifi_ap_t entry{};
            copy_string(entry.ssid, sizeof(entry.ssid), ssid);
            entry.signal = static_cast<int>(nm_access_point_get_strength(access_point));
            copy_string(entry.security, sizeof(entry.security), security_name(access_point));
            entry.in_use = ssid == active ? 1 : 0;
            entry.saved = profile_exists(ssid);

            auto found = by_ssid.find(ssid);
            if (found == by_ssid.end()) {
                if (output.size() < output.capacity()) {
                    by_ssid.emplace(ssid, output.size());
                    output.push_back(entry);
                }
            } else {
                cp0_wifi_ap_t &current = output[found->second];
                if (entry.signal > current.signal)
                    current = entry;
                else {
                    current.in_use |= entry.in_use;
                    current.saved |= entry.saved;
                }
            }
            if (output.size() >= output.capacity())
                break;
        }
        return static_cast<int>(output.size());
    }
};

using WifiRegistration = cp0::SignalRegistration<decltype(cp0_signal_wifi_api)>;

struct Runtime {
    std::mutex mutex;
    WifiRegistration registration;
    std::shared_ptr<NmtuiWifiAdapter> adapter;
    std::string last_error;
};

Runtime &runtime()
{
    static Runtime value;
    return value;
}

} // namespace

extern "C" int nmtui_wifi_init(void)
{
    Runtime &state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.adapter && state.registration.registered())
        return 0;

    try {
        auto adapter = std::make_shared<NmtuiWifiAdapter>();
        if (!state.registration.replace(
                cp0_signal_wifi_api,
                [adapter](std::list<std::string> args,
                          std::function<void(int, std::string)> callback) {
                    adapter->api_call(std::move(args), std::move(callback));
                })) {
            state.last_error = "failed to register cp0_signal_wifi_api";
            return -1;
        }
        state.last_error.clear();
        state.adapter = std::move(adapter);
        return 0;
    } catch (...) {
        state.last_error = "failed to initialize nmtui Wi-Fi adapter";
        return -1;
    }
}

extern "C" void nmtui_wifi_deinit(void)
{
    Runtime &state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.registration.reset();
    state.adapter.reset();
}

extern "C" int nmtui_wifi_is_available(void)
{
    Runtime &state = runtime();
    std::shared_ptr<NmtuiWifiAdapter> adapter;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        adapter = state.adapter;
    }
    if (!adapter)
        return 0;
    return adapter->available() ? 1 : 0;
}

extern "C" const char *nmtui_wifi_last_error(void)
{
    thread_local std::string message;
    Runtime &state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.adapter)
        state.last_error = state.adapter->last_error();
    message = state.last_error;
    return message.c_str();
}

extern "C" void init_wifi(void)
{
    (void)nmtui_wifi_init();
}

extern "C" void deinit_wifi(void)
{
    nmtui_wifi_deinit();
}
