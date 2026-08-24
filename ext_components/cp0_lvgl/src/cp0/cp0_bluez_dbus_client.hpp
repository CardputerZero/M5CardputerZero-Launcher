#pragma once

#include "cp0_lvgl_app.h"

#include <functional>
#include <string>
#include <vector>

namespace cp0_bluez_dbus {

using Completion = std::function<void(int, const std::string &)>;
using SnapshotListener = std::function<void()>;

struct AgentRequest {
    uint64_t id = 0;
    std::string method;
    std::string device;
    std::string passkey;
    std::string uuid;
};

using AgentListener = std::function<void(const AgentRequest &)>;

void initialize();
void shutdown();
void subscribe(SnapshotListener listener);
void set_agent_listener(AgentListener listener);
void agent_reply(uint64_t id, bool accepted, const std::string &text);

cp0_bt_status_t status();
int list(cp0_bt_device_t *out, int max_devices, bool connected_only);

void set_power_async(int enabled, Completion completion);
void set_alias_async(const char *alias, Completion completion);
void set_discoverable_async(int enabled, Completion completion);
void start_discovery_async(Completion completion);
void stop_discovery_async(Completion completion);
void pair_async(const char *address, Completion completion);
void connect_async(const char *address, Completion completion);
void disconnect_async(const char *address, Completion completion);
void remove_async(const char *address, Completion completion);

int set_power(int enabled);
int set_alias(const char *alias);
int set_discoverable(int enabled);
int start_discovery();
int stop_discovery();
int pair(const char *address);
int connect(const char *address);
int disconnect(const char *address);
int remove(const char *address);

} // namespace cp0_bluez_dbus
