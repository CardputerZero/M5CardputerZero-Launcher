#include "cp0_bluez_dbus_client.hpp"
#include "cp0_lvgl_log.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <dbus/dbus.h>
}

namespace {

static constexpr const char *kBluezService = "org.bluez";
static constexpr const char *kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";
static constexpr const char *kPropertiesIface = "org.freedesktop.DBus.Properties";
static constexpr const char *kAdapterIface = "org.bluez.Adapter1";
static constexpr const char *kDeviceIface = "org.bluez.Device1";
static constexpr const char *kAgentManagerIface = "org.bluez.AgentManager1";
static constexpr const char *kAgentIface = "org.bluez.Agent1";
static constexpr const char *kAgentPath = "/com/cardputerzero/applaunch/agent";
// Keep synchronous BlueZ property calls below the UI's 3-second Power
// operation deadline. Otherwise the LVGL timeout can fire while the backend
// is still blocked in dbus_connection_send_with_reply_and_block().
static constexpr int kCallTimeoutMs = 2500;

struct DeviceInfo {
    std::string path;
    cp0_bt_device_t dev{};
};

struct ManagedSnapshot {
    std::string adapter_path;
    cp0_bt_status_t status{};
    std::vector<DeviceInfo> devices;
};

static void copy_string(char *dst, size_t dst_size, const std::string &src)
{
    if (!dst || dst_size == 0)
        return;
    std::strncpy(dst, src.c_str(), dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static bool is_bt_address(const char *address)
{
    if (!address || std::strlen(address) != 17)
        return false;
    for (int i = 0; i < 17; ++i) {
        unsigned char ch = static_cast<unsigned char>(address[i]);
        if ((i + 1) % 3 == 0) {
            if (ch != ':')
                return false;
        } else if (!std::isxdigit(ch)) {
            return false;
        }
    }
    return true;
}

static std::string uppercase_address(const char *address)
{
    std::string out(address ? address : "");
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return out;
}

class ScopedError {
public:
    ScopedError() { dbus_error_init(&err_); }
    ~ScopedError()
    {
        if (dbus_error_is_set(&err_))
            dbus_error_free(&err_);
    }
    DBusError *get() { return &err_; }
    bool is_set() const { return dbus_error_is_set(&err_); }
    const char *message() const { return err_.message ? err_.message : "unknown"; }

private:
    DBusError err_{};
};

class ScopedMessage {
public:
    explicit ScopedMessage(DBusMessage *msg = nullptr) : msg_(msg) {}
    ~ScopedMessage()
    {
        if (msg_)
            dbus_message_unref(msg_);
    }
    DBusMessage *get() const { return msg_; }
    DBusMessage *release()
    {
        DBusMessage *tmp = msg_;
        msg_ = nullptr;
        return tmp;
    }
    explicit operator bool() const { return msg_ != nullptr; }

private:
    DBusMessage *msg_ = nullptr;
};

static bool iter_is_type(DBusMessageIter *iter, int type)
{
    return dbus_message_iter_get_arg_type(iter) == type;
}

static bool append_basic_variant(DBusMessageIter *iter, const char *signature, int type, const void *value)
{
    DBusMessageIter variant;
    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, signature, &variant))
        return false;
    if (!dbus_message_iter_append_basic(&variant, type, value)) {
        dbus_message_iter_abandon_container(iter, &variant);
        return false;
    }
    return dbus_message_iter_close_container(iter, &variant);
}

class BluezDbusClient {
public:
    ~BluezDbusClient()
    {
        if (conn_ && agent_registered_) {
            unregister_agent();
            dbus_connection_unregister_object_path(conn_, kAgentPath);
        }
        if (conn_)
            dbus_connection_unref(conn_);
    }

    cp0_bt_status_t status()
    {
        ManagedSnapshot snapshot;
        if (!get_managed_objects(snapshot))
            return {};
        return snapshot.status;
    }

    int set_power(int on)
    {
        std::string adapter = adapter_path();
        if (adapter.empty())
            return -1;
        dbus_bool_t powered = on ? TRUE : FALSE;
        return set_property_bool(adapter.c_str(), kAdapterIface, "Powered", powered) ? 0 : -1;
    }

    int set_discoverable(int on)
    {
        std::string adapter = adapter_path();
        if (adapter.empty())
            return -1;
        dbus_bool_t discoverable = on ? TRUE : FALSE;
        return set_property_bool(adapter.c_str(), kAdapterIface, "Discoverable", discoverable) ? 0 : -1;
    }

    int set_alias(const char *alias)
    {
        if (!alias || !alias[0])
            return -1;
        std::string adapter = adapter_path();
        if (adapter.empty())
            return -1;
        return set_property_string(adapter.c_str(), kAdapterIface, "Alias", alias) ? 0 : -1;
    }

    int start_discovery()
    {
        ManagedSnapshot snapshot;
        if (!get_managed_objects(snapshot) || snapshot.adapter_path.empty())
            return -1;
        if (!snapshot.status.powered) {
            cp0_zmq_log("bt", "scan on rejected: adapter is powered off");
            return -1;
        }
        return call_no_args(snapshot.adapter_path.c_str(), kAdapterIface, "StartDiscovery") ? 0 : -1;
    }

    int stop_discovery()
    {
        std::string adapter = adapter_path();
        if (adapter.empty())
            return 0;
        return call_no_args(adapter.c_str(), kAdapterIface, "StopDiscovery") ? 0 : -1;
    }

    int list(cp0_bt_device_t *out, int max_devices, bool connected_only)
    {
        if (!out || max_devices <= 0)
            return 0;
        ManagedSnapshot snapshot;
        if (!get_managed_objects(snapshot))
            return -1;
        int count = 0;
        for (const DeviceInfo &info : snapshot.devices) {
            if (count >= max_devices)
                break;
            if (!info.dev.address[0])
                continue;
            if (connected_only && !info.dev.connected)
                continue;
            out[count++] = info.dev;
        }
        return count;
    }

    int pair(const char *address)
    {
        if (!is_bt_address(address))
            return -1;
        ensure_agent();
        return device_method(address, "Pair");
    }

    int connect(const char *address)
    {
        if (!is_bt_address(address))
            return -1;
        ensure_agent();
        return device_method(address, "Connect");
    }

    int disconnect(const char *address)
    {
        if (!is_bt_address(address))
            return -1;
        return device_method(address, "Disconnect");
    }

    int remove(const char *address)
    {
        if (!is_bt_address(address))
            return -1;
        ManagedSnapshot snapshot;
        if (!get_managed_objects(snapshot) || snapshot.adapter_path.empty())
            return -1;
        std::string device = find_device_path(snapshot, address);
        if (device.empty())
            return -1;

        ScopedMessage msg(dbus_message_new_method_call(kBluezService, snapshot.adapter_path.c_str(),
                                                       kAdapterIface, "RemoveDevice"));
        if (!msg)
            return -1;
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        const char *path = device.c_str();
        if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &path))
            return -1;
        return send_blocking(msg.get()) ? 0 : -1;
    }

private:
    DBusConnection *conn_ = nullptr;
    bool object_registered_ = false;
    bool agent_registered_ = false;
    std::mutex mutex_;

    bool ensure_connection()
    {
        if (conn_)
            return true;
        ScopedError err;
        conn_ = dbus_bus_get(DBUS_BUS_SYSTEM, err.get());
        if (err.is_set() || !conn_) {
            cp0_zmq_logf("bt", "system dbus connect failed: %s", err.message());
            return false;
        }
        dbus_connection_set_exit_on_disconnect(conn_, FALSE);
        return true;
    }

    DBusMessage *send_with_reply(DBusMessage *msg)
    {
        if (!ensure_connection() || !msg)
            return nullptr;
        ScopedError err;
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn_, msg, kCallTimeoutMs, err.get());
        if (err.is_set()) {
            cp0_zmq_logf("bt", "dbus call %s failed: %s", dbus_message_get_member(msg), err.message());
            return nullptr;
        }
        return reply;
    }

    bool send_blocking(DBusMessage *msg)
    {
        ScopedMessage reply(send_with_reply(msg));
        if (!reply)
            return false;
        return dbus_message_get_type(reply.get()) != DBUS_MESSAGE_TYPE_ERROR;
    }

    bool call_no_args(const char *path, const char *iface, const char *method)
    {
        ScopedMessage msg(dbus_message_new_method_call(kBluezService, path, iface, method));
        return msg && send_blocking(msg.get());
    }

    bool set_property_bool(const char *path, const char *iface, const char *property, dbus_bool_t value)
    {
        ScopedMessage msg(dbus_message_new_method_call(kBluezService, path, kPropertiesIface, "Set"));
        if (!msg)
            return false;
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        const char *iface_ptr = iface;
        const char *prop_ptr = property;
        if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface_ptr) ||
            !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop_ptr) ||
            !append_basic_variant(&iter, DBUS_TYPE_BOOLEAN_AS_STRING, DBUS_TYPE_BOOLEAN, &value))
            return false;
        return send_blocking(msg.get());
    }

    bool set_property_string(const char *path, const char *iface, const char *property, const char *value)
    {
        ScopedMessage msg(dbus_message_new_method_call(kBluezService, path, kPropertiesIface, "Set"));
        if (!msg)
            return false;
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        const char *iface_ptr = iface;
        const char *prop_ptr = property;
        const char *value_ptr = value ? value : "";
        if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface_ptr) ||
            !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop_ptr) ||
            !append_basic_variant(&iter, DBUS_TYPE_STRING_AS_STRING, DBUS_TYPE_STRING, &value_ptr))
            return false;
        return send_blocking(msg.get());
    }

    std::string adapter_path()
    {
        ManagedSnapshot snapshot;
        if (!get_managed_objects(snapshot))
            return {};
        return snapshot.adapter_path;
    }

    int device_method(const char *address, const char *method)
    {
        ManagedSnapshot snapshot;
        if (!get_managed_objects(snapshot))
            return -1;
        std::string path = find_device_path(snapshot, address);
        if (path.empty())
            return -1;
        return call_no_args(path.c_str(), kDeviceIface, method) ? 0 : -1;
    }

    static std::string find_device_path(const ManagedSnapshot &snapshot, const char *address)
    {
        std::string want = uppercase_address(address);
        for (const DeviceInfo &info : snapshot.devices) {
            if (uppercase_address(info.dev.address) == want)
                return info.path;
        }
        return {};
    }

    bool get_managed_objects(ManagedSnapshot &snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connection())
            return false;

        ScopedMessage msg(dbus_message_new_method_call(kBluezService, "/", kObjectManagerIface,
                                                       "GetManagedObjects"));
        if (!msg)
            return false;
        ScopedMessage reply(send_with_reply(msg.get()));
        if (!reply)
            return false;

        DBusMessageIter iter;
        if (!dbus_message_iter_init(reply.get(), &iter) || !iter_is_type(&iter, DBUS_TYPE_ARRAY))
            return false;
        parse_objects(&iter, snapshot);
        return !snapshot.adapter_path.empty();
    }

    static void parse_objects(DBusMessageIter *objects_iter, ManagedSnapshot &snapshot)
    {
        DBusMessageIter objects;
        dbus_message_iter_recurse(objects_iter, &objects);
        while (dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter object_entry;
            dbus_message_iter_recurse(&objects, &object_entry);

            const char *path = nullptr;
            dbus_message_iter_get_basic(&object_entry, &path);
            dbus_message_iter_next(&object_entry);
            if (iter_is_type(&object_entry, DBUS_TYPE_ARRAY))
                parse_interfaces(path ? path : "", &object_entry, snapshot);

            dbus_message_iter_next(&objects);
        }
    }

    static void parse_interfaces(const char *path, DBusMessageIter *ifaces_iter, ManagedSnapshot &snapshot)
    {
        DBusMessageIter ifaces;
        dbus_message_iter_recurse(ifaces_iter, &ifaces);
        while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter iface_entry;
            dbus_message_iter_recurse(&ifaces, &iface_entry);

            const char *iface = nullptr;
            dbus_message_iter_get_basic(&iface_entry, &iface);
            dbus_message_iter_next(&iface_entry);

            if (iface && iter_is_type(&iface_entry, DBUS_TYPE_ARRAY)) {
                if (!std::strcmp(iface, kAdapterIface)) {
                    if (snapshot.adapter_path.empty())
                        snapshot.adapter_path = path;
                    parse_adapter_properties(&iface_entry, snapshot.status);
                } else if (!std::strcmp(iface, kDeviceIface)) {
                    DeviceInfo info;
                    info.path = path;
                    parse_device_properties(&iface_entry, info.dev);
                    snapshot.devices.push_back(info);
                }
            }

            dbus_message_iter_next(&ifaces);
        }
    }

    static void parse_adapter_properties(DBusMessageIter *props_iter, cp0_bt_status_t &status)
    {
        DBusMessageIter props;
        dbus_message_iter_recurse(props_iter, &props);
        while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&props, &entry);
            const char *name = nullptr;
            dbus_message_iter_get_basic(&entry, &name);
            dbus_message_iter_next(&entry);
            if (name && iter_is_type(&entry, DBUS_TYPE_VARIANT)) {
                DBusMessageIter value;
                dbus_message_iter_recurse(&entry, &value);
                if (!std::strcmp(name, "Powered") && iter_is_type(&value, DBUS_TYPE_BOOLEAN)) {
                    dbus_bool_t powered = FALSE;
                    dbus_message_iter_get_basic(&value, &powered);
                    status.powered = powered ? 1 : 0;
                } else if (!std::strcmp(name, "Address") && iter_is_type(&value, DBUS_TYPE_STRING)) {
                    const char *address = "";
                    dbus_message_iter_get_basic(&value, &address);
                    copy_string(status.address, sizeof(status.address), address ? address : "");
                } else if (!std::strcmp(name, "Discoverable") && iter_is_type(&value, DBUS_TYPE_BOOLEAN)) {
                    dbus_bool_t discoverable = FALSE;
                    dbus_message_iter_get_basic(&value, &discoverable);
                    status.discoverable = discoverable ? 1 : 0;
                } else if (!std::strcmp(name, "Alias") && iter_is_type(&value, DBUS_TYPE_STRING)) {
                    const char *alias = "";
                    dbus_message_iter_get_basic(&value, &alias);
                    copy_string(status.alias, sizeof(status.alias), alias ? alias : "");
                }
            }
            dbus_message_iter_next(&props);
        }
    }

    static void parse_device_properties(DBusMessageIter *props_iter, cp0_bt_device_t &dev)
    {
        DBusMessageIter props;
        dbus_message_iter_recurse(props_iter, &props);
        while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&props, &entry);
            const char *name = nullptr;
            dbus_message_iter_get_basic(&entry, &name);
            dbus_message_iter_next(&entry);
            if (name && iter_is_type(&entry, DBUS_TYPE_VARIANT)) {
                DBusMessageIter value;
                dbus_message_iter_recurse(&entry, &value);
                parse_device_property(name, &value, dev);
            }
            dbus_message_iter_next(&props);
        }
    }

    static void parse_device_property(const char *name, DBusMessageIter *value, cp0_bt_device_t &dev)
    {
        int type = dbus_message_iter_get_arg_type(value);
        if (!std::strcmp(name, "Address") && type == DBUS_TYPE_STRING) {
            const char *address = "";
            dbus_message_iter_get_basic(value, &address);
            copy_string(dev.address, sizeof(dev.address), address ? address : "");
        } else if ((!std::strcmp(name, "Name") || (!dev.name[0] && !std::strcmp(name, "Alias"))) &&
                   type == DBUS_TYPE_STRING) {
            const char *text = "";
            dbus_message_iter_get_basic(value, &text);
            copy_string(dev.name, sizeof(dev.name), text ? text : "");
        } else if (!std::strcmp(name, "RSSI") && type == DBUS_TYPE_INT16) {
            dbus_int16_t rssi = 0;
            dbus_message_iter_get_basic(value, &rssi);
            dev.rssi = rssi;
        } else if (!std::strcmp(name, "Connected") && type == DBUS_TYPE_BOOLEAN) {
            dbus_bool_t v = FALSE;
            dbus_message_iter_get_basic(value, &v);
            dev.connected = v ? 1 : 0;
        } else if (!std::strcmp(name, "Paired") && type == DBUS_TYPE_BOOLEAN) {
            dbus_bool_t v = FALSE;
            dbus_message_iter_get_basic(value, &v);
            dev.paired = v ? 1 : 0;
        } else if (!std::strcmp(name, "Trusted") && type == DBUS_TYPE_BOOLEAN) {
            dbus_bool_t v = FALSE;
            dbus_message_iter_get_basic(value, &v);
            dev.trusted = v ? 1 : 0;
        }
    }

    void ensure_agent()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connection())
            return;
        register_object_path();
        register_agent();
    }

    void register_object_path()
    {
        if (object_registered_)
            return;
        static DBusObjectPathVTable vtable = {
            nullptr,
            BluezDbusClient::agent_message,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        };
        object_registered_ = dbus_connection_register_object_path(conn_, kAgentPath, &vtable, this);
        if (!object_registered_)
            cp0_zmq_log("bt", "register agent object path failed");
    }

    void register_agent()
    {
        if (agent_registered_ || !object_registered_)
            return;
        ScopedMessage msg(dbus_message_new_method_call(kBluezService, "/org/bluez",
                                                       kAgentManagerIface, "RegisterAgent"));
        if (!msg)
            return;
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        const char *path = kAgentPath;
        const char *capability = "NoInputNoOutput";
        if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &path) ||
            !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &capability))
            return;
        if (send_blocking(msg.get())) {
            agent_registered_ = true;
            request_default_agent();
        }
    }

    void request_default_agent()
    {
        ScopedMessage msg(dbus_message_new_method_call(kBluezService, "/org/bluez",
                                                       kAgentManagerIface, "RequestDefaultAgent"));
        if (!msg)
            return;
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        const char *path = kAgentPath;
        if (dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &path))
            send_blocking(msg.get());
    }

    void unregister_agent()
    {
        ScopedMessage msg(dbus_message_new_method_call(kBluezService, "/org/bluez",
                                                       kAgentManagerIface, "UnregisterAgent"));
        if (!msg)
            return;
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        const char *path = kAgentPath;
        if (dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &path))
            send_blocking(msg.get());
        agent_registered_ = false;
    }

    static DBusHandlerResult agent_message(DBusConnection *connection, DBusMessage *message, void *)
    {
        const char *iface = dbus_message_get_interface(message);
        const char *member = dbus_message_get_member(message);
        if (!iface || std::strcmp(iface, kAgentIface) != 0 || !member)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        ScopedMessage reply(create_agent_reply(message, member));
        if (!reply)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        dbus_connection_send(connection, reply.get(), nullptr);
        dbus_connection_flush(connection);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    static DBusMessage *create_agent_reply(DBusMessage *message, const char *member)
    {
        if (!std::strcmp(member, "RequestPinCode")) {
            DBusMessage *reply = dbus_message_new_method_return(message);
            if (!reply)
                return nullptr;
            const char *pin = "0000";
            if (!dbus_message_append_args(reply, DBUS_TYPE_STRING, &pin, DBUS_TYPE_INVALID)) {
                dbus_message_unref(reply);
                return nullptr;
            }
            return reply;
        }
        if (!std::strcmp(member, "RequestPasskey")) {
            DBusMessage *reply = dbus_message_new_method_return(message);
            if (!reply)
                return nullptr;
            dbus_uint32_t passkey = 0;
            if (!dbus_message_append_args(reply, DBUS_TYPE_UINT32, &passkey, DBUS_TYPE_INVALID)) {
                dbus_message_unref(reply);
                return nullptr;
            }
            return reply;
        }
        return dbus_message_new_method_return(message);
    }
};

static BluezDbusClient &client()
{
    static BluezDbusClient c;
    return c;
}

} // namespace

namespace cp0_bluez_dbus {

cp0_bt_status_t status() { return client().status(); }
int set_power(int on) { return client().set_power(on); }
int set_alias(const char *alias) { return client().set_alias(alias); }
int set_discoverable(int on) { return client().set_discoverable(on); }
int start_discovery() { return client().start_discovery(); }
int stop_discovery() { return client().stop_discovery(); }
int list(cp0_bt_device_t *out, int max_devices, bool connected_only)
{
    return client().list(out, max_devices, connected_only);
}
int pair(const char *address) { return client().pair(address); }
int connect(const char *address) { return client().connect(address); }
int disconnect(const char *address) { return client().disconnect(address); }
int remove(const char *address) { return client().remove(address); }

} // namespace cp0_bluez_dbus
