#pragma once

#include "cp0_lvgl_app.h"

#include <functional>
#include <list>
#include <string>

namespace cp0::bluetooth {

// Backwards-compatible commands remain as-is. Session-scoped commands carry a
// session id argument so a BluetoothBackendSession can own the underlying
// BlueZ client / scan thread / async futures without ever touching UI state.
enum class Command {
    // Legacy (session-less, synchronous) commands.
    Status, Power, Alias, Discoverable, Scan, DiscoveryStart, DiscoveryStop,
    List, ConnectedList, Pair, Connect, Disconnect, Remove,

    // Session lifecycle.
    SessionInit, SessionDeinit,

    // Session-scoped async status read.
    StatusGet,

    // Connected sub-page lifecycle.
    ConnectedListInit, ConnectedListGet, ConnectedListDeinit,

    // Scan sub-page lifecycle.
    ScanOn, ScanOff,
};

struct Request {
    Command command = Command::Status;
    int value = 0;
    int max_count = 16;
    std::string text;       // device address or alias payload
    std::string session_id; // set for session-scoped commands
    bool has_session = false;
};

struct Reply {
    int code = -1;
    std::string data;
};

bool parse_request(const std::list<std::string> &arguments, Request &request);
bool valid_session_id(const std::string &session_id);
std::string sanitize_wire_field(std::string value);
void invoke_callback(const std::function<void(int, std::string)> &callback,
                     int code, const std::string &data) noexcept;

// Shared wire encoding so the hardware and SDL backends produce identical
// payloads. The UI decodes these with BluetoothPageModel::decode_*_record.
std::string encode_status(const cp0_bt_status_t &status);
std::string encode_devices(const cp0_bt_device_t *devices, int count);

template <typename BackendOperation>
void invoke_backend(const std::function<void(int, std::string)> &callback,
                    BackendOperation operation) noexcept
{
    Reply reply;
    try {
        reply = operation();
    } catch (...) {
        reply = {-1, "bluetooth backend failure"};
    }
    invoke_callback(callback, reply.code, reply.data);
}

} // namespace cp0::bluetooth
