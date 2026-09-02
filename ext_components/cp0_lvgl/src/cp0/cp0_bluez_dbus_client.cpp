#include "cp0_bluez_dbus_client.hpp"
#include "cp0_bluetooth_error_policy.hpp"
#include "cp0_lvgl_log.h"

#include <gio/gio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr const char *kBluez = "org.bluez";
constexpr const char *kProperties = "org.freedesktop.DBus.Properties";
constexpr const char *kAdapter = "org.bluez.Adapter1";
constexpr const char *kDevice = "org.bluez.Device1";
constexpr const char *kAgentManager = "org.bluez.AgentManager1";
constexpr const char *kAgent = "org.bluez.Agent1";
constexpr const char *kAgentPath = "/com/cardputerzero/applaunch/agent";
constexpr int kCallTimeoutMs = 15000;
constexpr int kPairTimeoutMs = 60000;
constexpr int kConnectTimeoutMs = 30000;

struct DeviceEntry {
    GDBusProxy *proxy = nullptr;
    cp0_bt_device_t value{};
};

struct Snapshot {
    cp0_bt_status_t status{};
    std::string adapter_path;
    GDBusProxy *adapter = nullptr;
    std::map<std::string, DeviceEntry> devices;
};

class BluezWorker {
public:
    ~BluezWorker() { stop(); }

    void start()
    {
        std::call_once(start_once_, [this] {
            {
                std::lock_guard<std::mutex> lock(lifecycle_mutex_);
                worker_exited_ = false;
                stopping_ = false;
            }
            stop_requested_.store(false, std::memory_order_release);
            thread_ = std::thread([this] { run(); });
        });
    }

    void stop()
    {
        if (!thread_.joinable())
            return;
        stop_requested_.store(true, std::memory_order_release);
        GMainContext *context = nullptr;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            stopping_ = true;
            if (context_)
                context = g_main_context_ref(context_);
        }
        lifecycle_cv_.notify_all();
        if (context) {
            g_main_context_invoke(context, [](gpointer data) -> gboolean {
                auto *self = static_cast<BluezWorker *>(data);
                if (self->loop_)
                    g_main_loop_quit(self->loop_);
                return G_SOURCE_REMOVE;
            }, this);
            g_main_context_unref(context);
        }
        thread_.join();
    }

    cp0_bt_status_t status()
    {
        if (!ensure_started())
            return {};
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        return snapshot_.status;
    }

    int list(cp0_bt_device_t *out, int max_devices, bool connected_only)
    {
        if (!ensure_started())
            return 0;
        if (!out || max_devices <= 0)
            return 0;
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        std::vector<const DeviceEntry *> ordered;
        ordered.reserve(snapshot_.devices.size());
        for (const auto &item : snapshot_.devices)
            ordered.push_back(&item.second);
        std::stable_sort(ordered.begin(), ordered.end(), [](const DeviceEntry *left, const DeviceEntry *right) {
            const auto rank = [](const DeviceEntry *entry) {
                return (entry->value.connected ? 4 : 0) +
                       (entry->value.paired ? 2 : 0) +
                       (has_real_name(entry->value) ? 1 : 0);
            };
            const int left_rank = rank(left);
            const int right_rank = rank(right);
            if (left_rank != right_rank)
                return left_rank > right_rank;
            return left->value.rssi > right->value.rssi;
        });
        int count = 0;
        for (const DeviceEntry *entry : ordered) {
            if (count >= max_devices)
                break;
            if (!entry->value.address[0])
                continue;
            if (connected_only && !entry->value.connected)
                continue;
            out[count++] = entry->value;
        }
        cp0_zmq_logf("bt", "cache list connected_only=%d count=%d cached_devices=%zu",
                     connected_only ? 1 : 0, count, snapshot_.devices.size());
        return count;
    }

    void subscribe(cp0_bluez_dbus::SnapshotListener listener)
    {
        if (!ensure_started())
            return;
        std::lock_guard<std::mutex> lock(listener_mutex_);
        listeners_.push_back(std::move(listener));
    }

    void command(const char *kind, const char *value, cp0_bluez_dbus::Completion completion)
    {
        if (!ensure_started()) {
            if (completion)
                completion(-1, "bluetooth worker unavailable");
            return;
        }
        auto *request = new (std::nothrow) CommandRequest{kind ? kind : "", value ? value : "", std::move(completion)};
        if (!request) {
            if (completion)
                completion(-1, "out of memory");
            return;
        }
        cp0_zmq_logf("bt", "command queued kind=%s value=%s", request->kind.c_str(), request->value.c_str());
        invoke([request](BluezWorker *self) {
            self->execute(request);
        });
    }

    void agent_reply(guint64 id, bool accepted, const std::string &text)
    {
        if (!ensure_started())
            return;
        invoke([id, accepted, text](BluezWorker *self) {
            self->complete_agent_request(id, accepted, text);
        });
    }

    void set_agent_listener(cp0_bluez_dbus::AgentListener listener)
    {
        if (!ensure_started())
            return;
        std::lock_guard<std::mutex> lock(agent_listener_mutex_);
        agent_listener_ = std::move(listener);
    }

private:
    static bool has_real_name(const cp0_bt_device_t &device)
    {
        if (!device.name[0])
            return false;
        std::string name_hex;
        std::string address_hex;
        for (const unsigned char character : std::string(device.name)) {
            if (std::isxdigit(character))
                name_hex.push_back(static_cast<char>(std::tolower(character)));
        }
        for (const unsigned char character : std::string(device.address)) {
            if (std::isxdigit(character))
                address_hex.push_back(static_cast<char>(std::tolower(character)));
        }
        return name_hex.size() != 12 || name_hex != address_hex;
    }

    struct PendingAgent {
        cp0_bluez_dbus::AgentRequest request;
        GDBusMethodInvocation *invocation = nullptr;
    };

    struct CommandRequest {
        std::string kind;
        std::string value;
        cp0_bluez_dbus::Completion completion;
        bool connection_call = false;
    };

    struct PairCleanupRequest {
        BluezWorker *worker = nullptr;
        std::string path;
        std::string reason;
        bool force = false;
    };

    std::once_flag start_once_;
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_cv_;
    bool worker_exited_ = false;
    bool stopping_ = false;
    bool context_ready_ = false;
    GMainContext *context_ = nullptr;
    GMainLoop *loop_ = nullptr;
    GDBusConnection *connection_ = nullptr;
    GDBusObjectManager *manager_ = nullptr;
    guint manager_retry_source_id_ = 0;
    guint agent_registration_id_ = 0;
    guint bluez_owner_watch_id_ = 0;
    bool agent_registered_ = false;
    guint64 next_agent_request_id_ = 1;
    std::map<guint64, PendingAgent> agent_requests_;
    std::string pairing_device_path_;
    std::mutex agent_listener_mutex_;
    cp0_bluez_dbus::AgentListener agent_listener_;

    std::mutex snapshot_mutex_;
    Snapshot snapshot_;
    std::mutex listener_mutex_;
    std::vector<cp0_bluez_dbus::SnapshotListener> listeners_;

    static const GDBusInterfaceVTable &agent_vtable()
    {
        static const GDBusInterfaceVTable table = {
            [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *method,
               GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data) {
                static_cast<BluezWorker *>(user_data)->handle_agent_call(method, parameters, invocation);
            },
            nullptr,
            nullptr,
            {},
        };
        return table;
    }

    static const GDBusInterfaceInfo *agent_info()
    {
        static GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(
            "<node><interface name='org.bluez.Agent1'>"
            "<method name='Release'/><method name='RequestPinCode'><arg name='device' type='o' direction='in'/><arg name='pincode' type='s' direction='out'/></method>"
            "<method name='DisplayPinCode'><arg name='device' type='o' direction='in'/><arg name='pincode' type='s' direction='in'/></method>"
            "<method name='RequestPasskey'><arg name='device' type='o' direction='in'/><arg name='passkey' type='u' direction='out'/></method>"
            "<method name='DisplayPasskey'><arg name='device' type='o' direction='in'/><arg name='passkey' type='u' direction='in'/><arg name='entered' type='q' direction='in'/></method>"
            "<method name='RequestConfirmation'><arg name='device' type='o' direction='in'/><arg name='passkey' type='u' direction='in'/></method>"
            "<method name='RequestAuthorization'><arg name='device' type='o' direction='in'/></method>"
            "<method name='AuthorizeService'><arg name='device' type='o' direction='in'/><arg name='uuid' type='s' direction='in'/></method>"
            "<method name='Cancel'/></interface></node>", nullptr);
        return node ? g_dbus_node_info_lookup_interface(node, kAgent) : nullptr;
    }

    bool ensure_started()
    {
        start();
        std::unique_lock<std::mutex> lock(lifecycle_mutex_);
        lifecycle_cv_.wait(lock, [this] { return context_ready_ || worker_exited_ || stopping_; });
        return context_ready_ && !stopping_;
    }

    template <typename Function>
    bool invoke(Function function)
    {
        auto *task = new std::function<void(BluezWorker *)>(std::move(function));
        GMainContext *context = nullptr;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            if (!context_ready_ || stopping_ || !context_) {
                delete task;
                return false;
            }
            context = g_main_context_ref(context_);
        }
        g_main_context_invoke_full(context, G_PRIORITY_DEFAULT, [](gpointer data) -> gboolean {
            std::unique_ptr<std::function<void(BluezWorker *)>> task(static_cast<std::function<void(BluezWorker *)> *>(data));
            (*task)(instance());
            return G_SOURCE_REMOVE;
        }, task, nullptr);
        g_main_context_unref(context);
        return true;
    }

    static BluezWorker *&instance_slot()
    {
        static BluezWorker *value = nullptr;
        return value;
    }

    static BluezWorker *instance()
    {
        return instance_slot();
    }

    void run()
    {
        cp0_zmq_log("bt", "worker starting");
        instance_slot() = this;
        GMainContext *context = g_main_context_new();
        GMainLoop *loop = g_main_loop_new(context, FALSE);
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            context_ = context;
            loop_ = loop;
            context_ready_ = true;
        }
        lifecycle_cv_.notify_all();
        g_main_context_push_thread_default(context);
        GError *error = nullptr;
        if (stop_requested_.load(std::memory_order_acquire)) {
            g_main_loop_quit(loop);
        } else {
            connection_ = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
            if (!connection_) {
                cp0_zmq_logf("bt", "system bus connection failed: %s", error ? error->message : "unknown");
                g_clear_error(&error);
            } else {
                cp0_zmq_log("bt", "system bus connected");
                bluez_owner_watch_id_ = g_dbus_connection_signal_subscribe(
                    connection_, "org.freedesktop.DBus", "org.freedesktop.DBus",
                    "NameOwnerChanged", "/org/freedesktop/DBus", kBluez,
                    G_DBUS_SIGNAL_FLAGS_NONE, bluez_owner_changed, this, nullptr);
                if (!setup_manager() || !agent_registered_)
                    schedule_manager_retry();
            }
        }
        if (!stop_requested_.load(std::memory_order_acquire))
            g_main_loop_run(loop);
        if (manager_retry_source_id_) {
            g_source_remove(manager_retry_source_id_);
            manager_retry_source_id_ = 0;
        }
        if (bluez_owner_watch_id_) {
            g_dbus_connection_signal_unsubscribe(connection_, bluez_owner_watch_id_);
            bluez_owner_watch_id_ = 0;
        }
        unregister_agent();
        clear_cache();
        if (manager_) g_object_unref(manager_);
        manager_ = nullptr;
        if (connection_) g_object_unref(connection_);
        connection_ = nullptr;
        g_main_loop_unref(loop);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            context_ = nullptr;
            loop_ = nullptr;
            context_ready_ = false;
            worker_exited_ = true;
        }
        lifecycle_cv_.notify_all();
        instance_slot() = nullptr;
        cp0_zmq_log("bt", "worker stopped");
    }

    bool setup_manager()
    {
        if (!connection_ || manager_)
            return manager_ != nullptr;

        GError *error = nullptr;
        manager_ = G_DBUS_OBJECT_MANAGER(g_dbus_object_manager_client_new_sync(
            connection_, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE, kBluez, "/", nullptr,
            nullptr, nullptr, nullptr, &error));
        if (!manager_) {
            cp0_zmq_logf("bt", "bluez object manager failed: %s", error ? error->message : "unknown");
            g_clear_error(&error);
            return false;
        }

        cp0_zmq_log("bt", "bluez object manager ready");
        g_signal_connect(manager_, "interface-added", G_CALLBACK(object_added), this);
        g_signal_connect(manager_, "interface-removed", G_CALLBACK(object_removed), this);
        g_signal_connect(manager_, "object-added", G_CALLBACK(object_object_added), this);
        g_signal_connect(manager_, "object-removed", G_CALLBACK(object_object_removed), this);
        sync_objects();
        register_agent();
        cp0_zmq_logf("bt", "initial cache adapter=%s devices=%zu",
                     snapshot_.adapter_path.c_str(), snapshot_.devices.size());
        return true;
    }

    void schedule_manager_retry()
    {
        if (manager_retry_source_id_ || !context_ || !connection_)
            return;
        GSource *source = g_timeout_source_new_seconds(2);
        g_source_set_priority(source, G_PRIORITY_DEFAULT);
        g_source_set_callback(source, [](gpointer data) -> gboolean {
            auto *self = static_cast<BluezWorker *>(data);
            if (!self->connection_) {
                self->manager_retry_source_id_ = 0;
                return G_SOURCE_REMOVE;
            }
            if (!self->manager_ && !self->setup_manager())
                return G_SOURCE_CONTINUE;
            if (!self->manager_)
                return G_SOURCE_CONTINUE;
            gchar *owner = g_dbus_object_manager_client_get_name_owner(
                G_DBUS_OBJECT_MANAGER_CLIENT(self->manager_));
            const bool bluez_available = owner && owner[0];
            g_free(owner);
            if (!bluez_available)
                return G_SOURCE_CONTINUE;
            if (!self->agent_registered_)
                self->register_agent();
            if (self->manager_ && self->agent_registered_) {
                self->manager_retry_source_id_ = 0;
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
        }, this, nullptr);
        manager_retry_source_id_ = g_source_attach(source, context_);
        g_source_unref(source);
    }

    static void bluez_owner_changed(GDBusConnection *, const gchar *, const gchar *,
                                    const gchar *, const gchar *, GVariant *parameters,
                                    gpointer user_data)
    {
        auto *self = static_cast<BluezWorker *>(user_data);
        const gchar *name = nullptr;
        const gchar *old_owner = nullptr;
        const gchar *new_owner = nullptr;
        g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
        (void)old_owner;
        if (!name || std::strcmp(name, kBluez) != 0)
            return;
        if (!new_owner || !new_owner[0]) {
            self->agent_registered_ = false;
            self->cancel_agent_requests("bluetooth service stopped");
            self->pairing_device_path_.clear();
            if (self->agent_registration_id_) {
                g_dbus_connection_unregister_object(self->connection_, self->agent_registration_id_);
                self->agent_registration_id_ = 0;
            }
            // GDBusObjectManagerClient may keep the old manager object alive
            // across a name-owner change. Drop it explicitly so the next
            // owner is represented by a fresh proxy/cache instead of a stale
            // manager that prevents setup_manager() from retrying.
            if (self->manager_) {
                g_object_unref(self->manager_);
                self->manager_ = nullptr;
            }
            self->clear_cache();
            self->publish();
            return;
        }
        if (!self->manager_ || !self->agent_registered_)
            self->schedule_manager_retry();
        if (!self->agent_registered_ && self->connection_)
            self->invoke([](BluezWorker *worker) {
                if (!worker->agent_registered_ && worker->connection_)
                    worker->register_agent();
            });
    }

    static void object_added(GDBusObjectManager *, GDBusObject *object, GDBusInterface *interface, gpointer user_data)
    {
        auto *self = static_cast<BluezWorker *>(user_data);
        self->update_interface(G_DBUS_OBJECT_PROXY(object), G_DBUS_PROXY(interface));
    }

    static void object_object_added(GDBusObjectManager *, GDBusObject *object, gpointer user_data)
    {
        auto *self = static_cast<BluezWorker *>(user_data);
        GList *interfaces = g_dbus_object_get_interfaces(object);
        for (GList *node = interfaces; node; node = node->next)
            self->update_interface(G_DBUS_OBJECT_PROXY(object), G_DBUS_PROXY(node->data));
        g_list_free_full(interfaces, g_object_unref);
        self->publish();
    }

    static void object_removed(GDBusObjectManager *, GDBusObject *object, GDBusInterface *interface, gpointer user_data)
    {
        auto *self = static_cast<BluezWorker *>(user_data);
        const char *path = g_dbus_object_get_object_path(object);
        const char *name = g_dbus_proxy_get_interface_name(G_DBUS_PROXY(interface));
        self->remove_interface(path, name);
    }

    static void object_object_removed(GDBusObjectManager *, GDBusObject *object, gpointer user_data)
    {
        auto *self = static_cast<BluezWorker *>(user_data);
        const char *path = g_dbus_object_get_object_path(object);
        {
            std::lock_guard<std::mutex> lock(self->snapshot_mutex_);
            auto found = self->snapshot_.devices.find(path ? path : "");
            if (found != self->snapshot_.devices.end()) {
                if (found->second.proxy) g_object_unref(found->second.proxy);
                self->snapshot_.devices.erase(found);
            }
            if (self->snapshot_.adapter_path == (path ? path : "")) {
                if (self->snapshot_.adapter) g_object_unref(self->snapshot_.adapter);
                self->snapshot_.adapter = nullptr;
                self->snapshot_.adapter_path.clear();
                self->snapshot_.status = {};
            }
        }
        self->publish();
    }

    void sync_objects()
    {
        if (!manager_) return;
        GList *objects = g_dbus_object_manager_get_objects(manager_);
        for (GList *node = objects; node; node = node->next) {
            auto *object = G_DBUS_OBJECT_PROXY(node->data);
            GList *interfaces = g_dbus_object_get_interfaces(G_DBUS_OBJECT(object));
            for (GList *iface = interfaces; iface; iface = iface->next)
                update_interface(object, G_DBUS_PROXY(iface->data));
            g_list_free_full(interfaces, g_object_unref);
        }
        g_list_free_full(objects, g_object_unref);
        publish();
    }

    void update_interface(GDBusObjectProxy *, GDBusProxy *proxy)
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        const char *path = g_dbus_proxy_get_object_path(proxy);
        const char *name = g_dbus_proxy_get_interface_name(proxy);
        if (!path || !name) return;
        if (std::strcmp(name, kAdapter) == 0) {
            if (snapshot_.adapter) g_object_unref(snapshot_.adapter);
            snapshot_.adapter = G_DBUS_PROXY(g_object_ref(proxy));
            snapshot_.adapter_path = path;
            connect_proxy(proxy);
            read_adapter(proxy);
        } else if (std::strcmp(name, kDevice) == 0) {
            auto found = snapshot_.devices.find(path);
            if (found != snapshot_.devices.end() && found->second.proxy)
                g_object_unref(found->second.proxy);
            DeviceEntry entry;
            entry.proxy = G_DBUS_PROXY(g_object_ref(proxy));
            snapshot_.devices[path] = entry;
            connect_proxy(proxy);
            read_device(proxy, snapshot_.devices[path].value);
        }
    }

    void remove_interface(const char *path, const char *name)
    {
        if (!path || !name) return;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            if (std::strcmp(name, kAdapter) == 0 && snapshot_.adapter_path == path) {
                if (snapshot_.adapter) g_object_unref(snapshot_.adapter);
                snapshot_.adapter = nullptr;
                snapshot_.adapter_path.clear();
                snapshot_.status = {};
            }
            if (std::strcmp(name, kDevice) == 0) {
                auto found = snapshot_.devices.find(path);
                if (found != snapshot_.devices.end()) {
                    if (found->second.proxy) g_object_unref(found->second.proxy);
                    snapshot_.devices.erase(found);
                }
            }
        }
        publish();
    }

    void connect_proxy(GDBusProxy *proxy)
    {
        static const char kPropertiesConnected[] = "cp0-bt-properties-connected";
        if (g_object_get_data(G_OBJECT(proxy), kPropertiesConnected))
            return;
        g_object_set_data(G_OBJECT(proxy), kPropertiesConnected, this);
        g_signal_connect(proxy, "g-properties-changed", G_CALLBACK(properties_changed), this);
    }

    static void properties_changed(GDBusProxy *proxy, GVariant *, const gchar * const *, gpointer user_data)
    {
        auto *self = static_cast<BluezWorker *>(user_data);
        {
            std::lock_guard<std::mutex> lock(self->snapshot_mutex_);
            const char *path = g_dbus_proxy_get_object_path(proxy);
            const char *name = g_dbus_proxy_get_interface_name(proxy);
            if (std::strcmp(name, kAdapter) == 0)
                self->read_adapter(proxy);
            else if (std::strcmp(name, kDevice) == 0) {
                auto found = self->snapshot_.devices.find(path ? path : "");
                if (found != self->snapshot_.devices.end())
                    self->read_device(proxy, found->second.value);
            }
        }
        self->publish();
    }

    static std::string string_property(GDBusProxy *proxy, const char *name)
    {
        GVariant *value = g_dbus_proxy_get_cached_property(proxy, name);
        if (!value || !g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            if (value) g_variant_unref(value);
            return {};
        }
        std::string result = g_variant_get_string(value, nullptr);
        g_variant_unref(value);
        return result;
    }

    static int int_property(GDBusProxy *proxy, const char *name, int fallback = 0)
    {
        GVariant *value = g_dbus_proxy_get_cached_property(proxy, name);
        if (!value) return fallback;
        int result = fallback;
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) result = g_variant_get_boolean(value) ? 1 : 0;
        else if (g_variant_is_of_type(value, G_VARIANT_TYPE_BYTE)) result = g_variant_get_byte(value);
        else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT16)) result = g_variant_get_int16(value);
        g_variant_unref(value);
        return result;
    }

    static void copy_string(char *dst, size_t size, const std::string &value)
    {
        if (!dst || !size) return;
        std::strncpy(dst, value.c_str(), size - 1);
        dst[size - 1] = '\0';
    }

    void read_adapter(GDBusProxy *proxy)
    {
        snapshot_.status.powered = int_property(proxy, "Powered");
        snapshot_.status.discoverable = int_property(proxy, "Discoverable");
        copy_string(snapshot_.status.address, sizeof(snapshot_.status.address), string_property(proxy, "Address"));
        std::string alias = string_property(proxy, "Alias");
        if (alias.empty()) alias = string_property(proxy, "Name");
        copy_string(snapshot_.status.alias, sizeof(snapshot_.status.alias), alias);
    }

    void read_device(GDBusProxy *proxy, cp0_bt_device_t &device)
    {
        copy_string(device.address, sizeof(device.address), string_property(proxy, "Address"));
        std::string name = string_property(proxy, "Alias");
        if (name.empty()) name = string_property(proxy, "Name");
        copy_string(device.name, sizeof(device.name), name);
        device.connected = int_property(proxy, "Connected");
        device.paired = int_property(proxy, "Paired");
        device.trusted = int_property(proxy, "Trusted");
        device.rssi = int_property(proxy, "RSSI", 0);
    }

    void publish()
    {
        std::vector<cp0_bluez_dbus::SnapshotListener> listeners;
        {
            std::lock_guard<std::mutex> lock(listener_mutex_);
            listeners = listeners_;
        }
        for (auto &listener : listeners)
            if (listener) listener();
    }

    void clear_cache()
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        if (snapshot_.adapter) g_object_unref(snapshot_.adapter);
        snapshot_.adapter = nullptr;
        snapshot_.adapter_path.clear();
        snapshot_.status = {};
        for (auto &item : snapshot_.devices)
            if (item.second.proxy) g_object_unref(item.second.proxy);
        snapshot_.devices.clear();
    }

    void execute(CommandRequest *request)
    {
        cp0_zmq_logf("bt", "command execute kind=%s value=%s adapter=%s",
                     request->kind.c_str(), request->value.c_str(), snapshot_.adapter_path.c_str());
        if (!connection_ || !snapshot_.adapter) {
            finish(request, -1, "no bluetooth adapter");
            return;
        }
        if (request->kind == "power") {
            set_property("Powered", g_variant_new_boolean(request->value == "1"), request);
        } else if (request->kind == "alias") {
            set_property("Alias", g_variant_new_string(request->value.c_str()), request);
        } else if (request->kind == "discoverable") {
            set_property("Discoverable", g_variant_new_boolean(request->value == "1"), request);
        } else if (request->kind == "start") {
            cp0_zmq_log("bt", "StartDiscovery call");
            call_proxy(snapshot_.adapter, "StartDiscovery", nullptr, request);
        } else if (request->kind == "stop") {
            cp0_zmq_log("bt", "StopDiscovery call");
            call_proxy(snapshot_.adapter, "StopDiscovery", nullptr, request);
        } else {
            std::string path;
            for (const auto &item : snapshot_.devices)
                if (upper(item.second.value.address) == upper(request->value)) { path = item.first; break; }
            auto found = snapshot_.devices.find(path);
            if (found == snapshot_.devices.end()) {
                // BlueZ may remove the proxy while a failed pairing is being
                // unwound. Device1 paths are deterministic, so retain a
                // direct cleanup path for CancelPairing/RemoveDevice.
                // Device1 object paths are deterministic (`dev_<MAC>`).  A
                // Pair() completion can race the object-manager property
                // update, so allow all device operations to use this path
                // while the cache catches up, not only cleanup commands.
                if ((request->kind == "pair" || request->kind == "cancel_pairing" ||
                     request->kind == "connect" || request->kind == "disconnect" ||
                     request->kind == "remove") &&
                    !snapshot_.adapter_path.empty() && request->value.size() == 17) {
                    path = snapshot_.adapter_path + "/dev_" + upper(request->value);
                    std::replace(path.begin() + snapshot_.adapter_path.size() + 5,
                                 path.end(), ':', '_');
                } else {
                    finish(request, -1, "device not found");
                    return;
                }
            }
            const char *method = nullptr;
            if (request->kind == "pair") method = "Pair";
            else if (request->kind == "cancel_pairing") method = "CancelPairing";
            else if (request->kind == "connect") method = "Connect";
            else if (request->kind == "disconnect") method = "Disconnect";
            else if (request->kind == "remove") {
                GVariant *parameters = g_variant_new("(o)", path.c_str());
                call_path(snapshot_.adapter, "RemoveDevice", parameters, request);
                return;
            }
            if (!method) { finish(request, -1, "unknown bluetooth command"); return; }
            if (request->kind == "pair")
                pairing_device_path_ = path;
            if (found != snapshot_.devices.end())
                call_proxy(found->second.proxy, method, nullptr, request);
            else {
                request->connection_call = true;
                g_dbus_connection_call(connection_, kBluez, path.c_str(), kDevice,
                                       method, nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE,
                                       kCallTimeoutMs, nullptr, method_done, request);
            }
        }
    }

    static std::string upper(const std::string &text)
    {
        std::string result = text;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return result;
    }

    void set_property(const char *property, GVariant *value, CommandRequest *request)
    {
        GVariant *parameters = g_variant_new("(ssv)", kAdapter, property, value);
        request->connection_call = true;
        g_dbus_connection_call(connection_, kBluez, snapshot_.adapter_path.c_str(), kProperties, "Set",
                               parameters, nullptr, G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr,
                               method_done, request);
    }

    void call_path(GDBusProxy *proxy, const char *method, GVariant *parameters, CommandRequest *request)
    {
            call_proxy(proxy, method, parameters, request);
    }

    void call_proxy(GDBusProxy *proxy, const char *method, GVariant *parameters, CommandRequest *request)
    {
        int timeout_ms = kCallTimeoutMs;
        if (request->kind == "pair") timeout_ms = kPairTimeoutMs;
        else if (request->kind == "connect") timeout_ms = kConnectTimeoutMs;
        g_dbus_proxy_call(proxy, method, parameters, G_DBUS_CALL_FLAGS_NONE,
                          timeout_ms, nullptr, method_done, request);
    }

    static void method_done(GObject *source, GAsyncResult *result, gpointer user_data)
    {
        auto *request = static_cast<CommandRequest *>(user_data);
        GError *error = nullptr;
        GVariant *reply = nullptr;
        if (request->connection_call)
            reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
        else
            reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), result, &error);
        // BlueZ operations are intentionally idempotent at the Settings API
        // boundary. Pair() commonly leaves Device1 connected, so the UI's
        // follow-up Connect() can legitimately return AlreadyConnected. The
        // same applies to repeated Disconnect() and discovery calls.
        bool idempotent_success = false;
        bool connect_already_established = false;
        std::string error_name;
        if (error) {
            gchar *remote_error = g_dbus_error_get_remote_error(error);
            error_name = remote_error ? remote_error : "";
            g_free(remote_error);
            idempotent_success = cp0::bluetooth::policy::is_idempotent_success(
                request->kind, error_name);

            // Device1.Connect may wait for profile setup and time out even
            // though BlueZ has already raised Connected=true. Trust the
            // authoritative cached property instead of surfacing a false UI
            // failure in that case.
            if (request->kind == "connect") {
                if (auto *self = instance()) {
                    std::lock_guard<std::mutex> lock(self->snapshot_mutex_);
                    for (const auto &item : self->snapshot_.devices) {
                        if (cp0::bluetooth::policy::connected_snapshot_matches(
                                request->value, item.second.value.address,
                                item.second.value.connected)) {
                            connect_already_established = true;
                            break;
                        }
                    }
                }
            }
        }
        idempotent_success = idempotent_success || connect_already_established;
        const int code = (!error && reply) || idempotent_success ? 0 : -1;
        const std::string message = idempotent_success ? "ok" : (error ? error->message : "ok");

        if (code == 0 && request->kind == "discoverable") {
            if (auto *self = instance()) {
                std::lock_guard<std::mutex> lock(self->snapshot_mutex_);
                self->snapshot_.status.discoverable = request->value == "1";
            }
        }

        cp0_zmq_logf("bt", "command complete kind=%s code=%d message=%s",
                     request->kind.c_str(), code, message.c_str());
        if (request->kind == "pair") {
            if (auto *self = instance()) {
                if (code != 0 &&
                    !cp0::bluetooth::policy::is_pair_already_exists(error_name)) {
                    // The target is cleaned below with force=true. Avoid
                    // scheduling a second non-forced cleanup for the same
                    // Device1 path while cancelling pending Agent calls.
                    self->cancel_agent_requests("pair failed", false);
                    // Pair() can fail without issuing an Agent1 request (for
                    // example when BlueZ rejects an existing bond). Ensure
                    // the transient Device1 object is removed in that case
                    // as well, so the next attempt does not reuse stale
                    // authentication state.
                    if (!self->pairing_device_path_.empty())
                        self->cancel_pairing_then_remove(self->pairing_device_path_, "pair failed",
                                                         cp0::bluetooth::policy::pair_requires_force_cleanup(error_name));
                }
                self->pairing_device_path_.clear();
            }
        }
        if (reply) g_variant_unref(reply);
        if (error) g_error_free(error);
        auto completion = std::move(request->completion);
        if (completion) completion(code, message);
        delete request;
    }

    static void finish(CommandRequest *request, int code, const char *message)
    {
        auto completion = std::move(request->completion);
        if (completion) completion(code, message ? message : "error");
        delete request;
    }

    void register_agent()
    {
        if (!connection_ || agent_registered_) return;
        GError *error = nullptr;
        agent_registration_id_ = g_dbus_connection_register_object(
            connection_, kAgentPath, const_cast<GDBusInterfaceInfo *>(agent_info()), &agent_vtable(), this, nullptr, &error);
        if (!agent_registration_id_) {
            cp0_zmq_logf("bt", "agent registration failed: %s", error ? error->message : "unknown");
            g_clear_error(&error);
            return;
        }

        GVariant *reply = g_dbus_connection_call_sync(
            connection_, kBluez, "/org/bluez", kAgentManager, "RegisterAgent",
            g_variant_new("(os)", kAgentPath, "KeyboardDisplay"),
            nullptr, G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr, &error);
        if (!reply) {
            cp0_zmq_logf("bt", "RegisterAgent failed: %s", error ? error->message : "unknown");
            g_clear_error(&error);
            g_dbus_connection_unregister_object(connection_, agent_registration_id_);
            agent_registration_id_ = 0;
            return;
        }
        g_variant_unref(reply);

        reply = g_dbus_connection_call_sync(
            connection_, kBluez, "/org/bluez", kAgentManager, "RequestDefaultAgent",
            g_variant_new("(o)", kAgentPath), nullptr, G_DBUS_CALL_FLAGS_NONE,
            kCallTimeoutMs, nullptr, &error);
        if (!reply) {
            cp0_zmq_logf("bt", "RequestDefaultAgent failed: %s", error ? error->message : "unknown");
            g_clear_error(&error);
            g_dbus_connection_call_sync(
                connection_, kBluez, "/org/bluez", kAgentManager, "UnregisterAgent",
                g_variant_new("(o)", kAgentPath), nullptr, G_DBUS_CALL_FLAGS_NONE,
                kCallTimeoutMs, nullptr, nullptr);
            g_dbus_connection_unregister_object(connection_, agent_registration_id_);
            agent_registration_id_ = 0;
            return;
        }
        g_variant_unref(reply);
        agent_registered_ = true;
    }

    void unregister_agent()
    {
        if (!connection_)
            return;

        // Complete any outstanding Agent1 calls before unregistering the
        // object. BlueZ keeps those calls pending until the agent answers or
        // is explicitly canceled; dropping the object first leaves the
        // pairing transaction stuck in a pending state.
        cancel_agent_requests("agent stopped");

        if (agent_registered_) {
            GError *error = nullptr;
            GVariant *reply = g_dbus_connection_call_sync(
                connection_, kBluez, "/org/bluez", kAgentManager, "UnregisterAgent",
                g_variant_new("(o)", kAgentPath), nullptr, G_DBUS_CALL_FLAGS_NONE,
                kCallTimeoutMs, nullptr, &error);
            if (!reply) {
                cp0_zmq_logf("bt", "UnregisterAgent failed: %s",
                             error ? error->message : "unknown");
            } else {
                g_variant_unref(reply);
            }
            g_clear_error(&error);
        }

        if (agent_registration_id_) g_dbus_connection_unregister_object(connection_, agent_registration_id_);
        agent_registration_id_ = 0;
        agent_registered_ = false;
        pairing_device_path_.clear();
    }

    void handle_agent_call(const char *method, GVariant *parameters, GDBusMethodInvocation *invocation)
    {
        if (!method) return;
        if (std::strcmp(method, "Release") == 0) {
            g_dbus_method_invocation_return_value(invocation, nullptr);
            cancel_agent_requests("agent released");
            return;
        }
        if (std::strcmp(method, "Cancel") == 0) {
            g_dbus_method_invocation_return_value(invocation, nullptr);
            cancel_agent_requests("pairing canceled");
            return;
        }
        if (std::strcmp(method, "DisplayPinCode") == 0 || std::strcmp(method, "DisplayPasskey") == 0) {
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        const guint64 id = next_agent_request_id_++;
        cp0_bluez_dbus::AgentRequest request{};
        request.id = id;
        request.method = method;
        if (parameters) {
            const gchar *device = nullptr;
            if (g_variant_n_children(parameters) > 0)
                g_variant_get_child(parameters, 0, "&o", &device);
            request.device = device ? device : "";
            {
                std::lock_guard<std::mutex> lock(snapshot_mutex_);
                const auto found = snapshot_.devices.find(request.device);
                if (found != snapshot_.devices.end()) {
                    request.paired = found->second.value.paired != 0;
                    request.trusted = found->second.value.trusted != 0;
                }
            }
            if (std::strcmp(method, "RequestConfirmation") == 0 && g_variant_n_children(parameters) > 1) {
                guint32 passkey = 0;
                g_variant_get_child(parameters, 1, "u", &passkey);
                request.passkey = std::to_string(passkey);
            } else if (std::strcmp(method, "AuthorizeService") == 0 && g_variant_n_children(parameters) > 1) {
                const gchar *uuid = nullptr;
                g_variant_get_child(parameters, 1, "&s", &uuid);
                request.uuid = uuid ? uuid : "";
            }
        }
        agent_requests_.emplace(id, PendingAgent{request, static_cast<GDBusMethodInvocation *>(g_object_ref(invocation))});
        cp0_bluez_dbus::AgentListener listener;
        {
            std::lock_guard<std::mutex> lock(agent_listener_mutex_);
            listener = agent_listener_;
        }
        if (listener) {
            // Agent callbacks cross from the GLib worker into application
            // code. A UI subscriber must never be able to terminate the
            // worker thread by throwing from this boundary.
            try {
                listener(request);
            } catch (...) {
                complete_agent_request(id, false, "agent listener failed");
            }
            return;
        }
        // The settings UI has no separate confirmation dialog. Limit the
        // fallback to a pair operation already initiated for this exact
        // device; PIN/passkey requests still require a real agent listener.
        const bool pairing_target = !pairing_device_path_.empty() &&
                                     request.device == pairing_device_path_;
        if (pairing_target &&
            (std::strcmp(method, "RequestConfirmation") == 0 ||
             std::strcmp(method, "RequestAuthorization") == 0 ||
             std::strcmp(method, "AuthorizeService") == 0)) {
            g_dbus_method_invocation_return_value(invocation, nullptr);
            g_object_unref(agent_requests_.at(id).invocation);
            agent_requests_.erase(id);
            return;
        }
        g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "pairing agent is not connected");
        g_object_unref(agent_requests_.at(id).invocation);
        agent_requests_.erase(id);
    }

    void complete_agent_request(guint64 id, bool accepted, const std::string &text)
    {
        auto found = agent_requests_.find(id);
        if (found == agent_requests_.end()) return;
        const std::string device_path = found->second.request.device;
        bool cleanup_device = !accepted;
        GDBusMethodInvocation *invocation = found->second.invocation;
        if (!accepted) {
            g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "pairing rejected");
        } else if (found->second.request.method == "RequestPinCode") {
            // BlueZ accepts PIN strings up to 16 bytes. Reject empty or
            // control-containing values at the backend boundary so a buggy
            // UI cannot send malformed credentials into the agent call.
            const bool valid_pin = cp0::bluetooth::policy::agent_reply_valid(
                "RequestPinCode", true, text);
            if (!valid_pin) {
                cleanup_device = true;
                g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "invalid pin code");
            } else
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", text.c_str()));
        } else if (found->second.request.method == "RequestPasskey") {
            const bool valid_passkey = cp0::bluetooth::policy::agent_reply_valid(
                "RequestPasskey", true, text);
            if (!valid_passkey) {
                cleanup_device = true;
                g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "invalid passkey");
            } else {
                guint32 passkey = static_cast<guint32>(std::strtoul(text.c_str(), nullptr, 10));
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", passkey));
            }
        } else {
            g_dbus_method_invocation_return_value(invocation, nullptr);
        }
        g_object_unref(invocation);
        agent_requests_.erase(found);
        if (cleanup_device)
            cancel_pairing_then_remove(device_path, "agent request rejected", false);
    }

    void cancel_pairing_then_remove(const std::string &path, const char *reason, bool force)
    {
        if (path.empty() || !connection_ || snapshot_.adapter_path.empty())
            return;

        // Do not remove an established bond when merely rejecting an Agent
        // request. Pair() failure is the explicit force=true recovery path for
        // stale link keys.
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            const auto found = snapshot_.devices.find(path);
            if (!force && found != snapshot_.devices.end() && found->second.value.paired)
                return;
        }

        auto *cleanup = new (std::nothrow) PairCleanupRequest{
            this, path, reason ? reason : "pairing cleanup", force};
        if (!cleanup) {
            remove_device(path, reason, force);
            return;
        }
        cp0_zmq_logf("bt", "canceling pairing before device removal path=%s reason=%s",
                     path.c_str(), cleanup->reason.c_str());
        g_dbus_connection_call(
            connection_, kBluez, path.c_str(), kDevice, "CancelPairing", nullptr,
            nullptr, G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr,
            [](GObject *source, GAsyncResult *result, gpointer user_data) {
                std::unique_ptr<PairCleanupRequest> cleanup(
                    static_cast<PairCleanupRequest *>(user_data));
                GError *error = nullptr;
                GVariant *reply = g_dbus_connection_call_finish(
                    G_DBUS_CONNECTION(source), result, &error);
                if (error) {
                    // DoesNotExist/NotConnected is expected when BlueZ has
                    // already unwound the transaction; removal is still safe.
                    cp0_zmq_logf("bt", "CancelPairing during cleanup: %s",
                                 error->message ? error->message : "unknown");
                }
                if (reply) g_variant_unref(reply);
                if (error) g_error_free(error);
                if (cleanup->worker)
                    cleanup->worker->remove_device(
                        cleanup->path, cleanup->reason.c_str(), cleanup->force);
            }, cleanup);
    }

    void remove_device(const std::string &path, const char *reason, bool force = false)
    {
        if (path.empty() || !connection_ || snapshot_.adapter_path.empty())
            return;

        // Agent cancellations normally remove only transient, unpaired
        // objects. An explicit Pair() failure passes force=true as well,
        // because a stale Paired=true bond can hold an old link key and make
        // every phone retry report an incorrect pairing code.
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            const auto found = snapshot_.devices.find(path);
            if (!force && found != snapshot_.devices.end() && found->second.value.paired)
                return;
        }

        cp0_zmq_logf("bt", "removing unpaired device path=%s reason=%s",
                     path.c_str(), reason ? reason : "unknown");
        g_dbus_connection_call(connection_, kBluez, snapshot_.adapter_path.c_str(), kAdapter,
                                "RemoveDevice", g_variant_new("(o)", path.c_str()),
                                nullptr, G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
                                nullptr, nullptr, nullptr);
    }

    void cancel_agent_requests(const char *message, bool cleanup_devices = true)
    {
        std::vector<std::string> cleanup_paths;
        cleanup_paths.reserve(agent_requests_.size());
        for (auto &item : agent_requests_) {
            if (item.second.invocation)
                g_dbus_method_invocation_return_dbus_error(item.second.invocation, "org.bluez.Error.Canceled", message);
            if (item.second.invocation)
                g_object_unref(item.second.invocation);
            if (!item.second.request.device.empty())
                cleanup_paths.push_back(item.second.request.device);
        }
        agent_requests_.clear();
        std::sort(cleanup_paths.begin(), cleanup_paths.end());
        cleanup_paths.erase(std::unique(cleanup_paths.begin(), cleanup_paths.end()), cleanup_paths.end());
        if (cleanup_devices) {
            for (const auto &path : cleanup_paths)
                cancel_pairing_then_remove(path, message, false);
        }
    }
};

BluezWorker &worker()
{
    static BluezWorker value;
    return value;
}

void command(const char *kind, const char *value, cp0_bluez_dbus::Completion completion)
{
    worker().command(kind, value, std::move(completion));
}

} // namespace

namespace cp0_bluez_dbus {

void initialize() { worker().start(); }
void shutdown() { worker().stop(); }
void subscribe(SnapshotListener listener) { worker().subscribe(std::move(listener)); }
void set_agent_listener(AgentListener listener) { worker().set_agent_listener(std::move(listener)); }
void agent_reply(uint64_t id, bool accepted, const std::string &text) { worker().agent_reply(id, accepted, text); }
cp0_bt_status_t status() { return worker().status(); }
int list(cp0_bt_device_t *out, int max_devices, bool connected_only) { return worker().list(out, max_devices, connected_only); }

void set_power_async(int enabled, Completion completion) { command("power", enabled ? "1" : "0", std::move(completion)); }
void set_alias_async(const char *alias, Completion completion) { command("alias", alias, std::move(completion)); }
void set_discoverable_async(int enabled, Completion completion) { command("discoverable", enabled ? "1" : "0", std::move(completion)); }
void start_discovery_async(Completion completion) { command("start", nullptr, std::move(completion)); }
void stop_discovery_async(Completion completion) { command("stop", nullptr, std::move(completion)); }
void pair_async(const char *address, Completion completion) { command("pair", address, std::move(completion)); }
void cancel_pairing_async(const char *address, Completion completion) { command("cancel_pairing", address, std::move(completion)); }
void connect_async(const char *address, Completion completion) { command("connect", address, std::move(completion)); }
void disconnect_async(const char *address, Completion completion) { command("disconnect", address, std::move(completion)); }
void remove_async(const char *address, Completion completion) { command("remove", address, std::move(completion)); }

int set_power(int enabled) { set_power_async(enabled, {}); return 0; }
int set_alias(const char *alias) { set_alias_async(alias, {}); return 0; }
int set_discoverable(int enabled) { set_discoverable_async(enabled, {}); return 0; }
int start_discovery() { start_discovery_async({}); return 0; }
int stop_discovery() { stop_discovery_async({}); return 0; }
int pair(const char *address) { pair_async(address, {}); return 0; }
int connect(const char *address) { connect_async(address, {}); return 0; }
int disconnect(const char *address) { disconnect_async(address, {}); return 0; }
int remove(const char *address) { remove_async(address, {}); return 0; }

} // namespace cp0_bluez_dbus
