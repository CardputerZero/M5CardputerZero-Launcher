#pragma once

#include <string>

namespace cp0::bluetooth::policy {

// BlueZ reports these errors for operations that have already reached the
// requested state. They are success at the Settings API boundary.
bool is_idempotent_success(const std::string &command,
                           const std::string &error_name);

// AlreadyExists/AlreadyPaired means that Pair() found a valid existing bond;
// deleting it as recovery would make the next attempt less reliable.
bool is_pair_already_exists(const std::string &error_name);
bool pair_requires_force_cleanup(const std::string &error_name);

// Device1.Connected is authoritative even when a profile Connect call
// reports a transport/profile error or times out.
bool connected_snapshot_matches(const std::string &requested_address,
                                const std::string &snapshot_address,
                                bool connected);

// Agent methods that require an outstanding invocation and a UI response.
bool is_agent_request_method(const std::string &method);
bool is_agent_display_method(const std::string &method);

// Validate user-provided credentials at the Agent1 boundary. Rejections do
// not carry text and are always valid terminal responses.
bool agent_reply_valid(const std::string &method, bool accepted,
                       const std::string &text);
bool agent_reply_requires_cleanup(const std::string &method, bool accepted,
                                  const std::string &text);

} // namespace cp0::bluetooth::policy
