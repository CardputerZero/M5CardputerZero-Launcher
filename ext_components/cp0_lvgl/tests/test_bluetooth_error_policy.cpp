#include "cp0_bluetooth_error_policy.hpp"

#include <cassert>

int main()
{
    using namespace cp0::bluetooth::policy;

    assert(is_pair_already_exists("org.bluez.Error.AlreadyExists"));
    assert(is_pair_already_exists("org.bluez.Error.AlreadyPaired"));
    assert(is_idempotent_success("pair", "org.bluez.Error.AlreadyExists"));
    assert(is_idempotent_success("pair", "org.bluez.Error.AlreadyPaired"));
    assert(!pair_requires_force_cleanup("org.bluez.Error.AlreadyExists"));
    assert(!pair_requires_force_cleanup("org.bluez.Error.AlreadyPaired"));
    assert(!pair_requires_force_cleanup("org.bluez.Error.InProgress"));
    assert(pair_requires_force_cleanup("org.bluez.Error.AuthenticationCanceled"));
    assert(!is_idempotent_success("pair", "org.bluez.Error.InProgress"));

    assert(is_idempotent_success("connect", "org.bluez.Error.AlreadyConnected"));
    assert(is_idempotent_success("connect", "org.bluez.Error.InProgress"));
    assert(!is_idempotent_success("connect", "org.bluez.Error.NotConnected"));
    assert(is_idempotent_success("disconnect", "org.bluez.Error.NotConnected"));
    assert(is_idempotent_success("start", "org.bluez.Error.InProgress"));
    assert(is_idempotent_success("stop", "org.bluez.Error.NotReady"));
    assert(is_idempotent_success("stop", "org.bluez.Error.NotAuthorized"));
    assert(!is_idempotent_success("stop", "org.bluez.Error.InProgress"));
    assert(!is_idempotent_success("remove", "org.bluez.Error.AlreadyExists"));

    assert(connected_snapshot_matches("AA:bb:01:23:45:67",
                                      "aa:BB:01:23:45:67", true));
    assert(!connected_snapshot_matches("AA:bb:01:23:45:67",
                                       "aa:BB:01:23:45:67", false));
    assert(!connected_snapshot_matches("AA:bb:01:23:45:67",
                                       "aa:BB:01:23:45:68", true));

    assert(is_agent_request_method("RequestPinCode"));
    assert(is_agent_request_method("RequestPasskey"));
    assert(is_agent_request_method("RequestConfirmation"));
    assert(is_agent_request_method("RequestAuthorization"));
    assert(is_agent_request_method("AuthorizeService"));
    assert(!is_agent_request_method("Cancel"));
    assert(is_agent_display_method("DisplayPinCode"));
    assert(is_agent_display_method("DisplayPasskey"));
    assert(!is_agent_display_method("RequestPasskey"));

    assert(agent_reply_valid("RequestPinCode", true, "1234"));
    assert(agent_reply_valid("RequestPinCode", true, "a b"));
    assert(!agent_reply_valid("RequestPinCode", true, ""));
    assert(!agent_reply_valid("RequestPinCode", true, "123\n4"));
    assert(!agent_reply_valid("RequestPinCode", true, std::string(17, '1')));
    assert(agent_reply_valid("RequestPasskey", true, "012345"));
    assert(!agent_reply_valid("RequestPasskey", true, "12345"));
    assert(!agent_reply_valid("RequestPasskey", true, "12345x"));
    assert(agent_reply_valid("RequestConfirmation", true, "92655"));
    assert(agent_reply_valid("RequestAuthorization", true, ""));
    assert(agent_reply_valid("AuthorizeService", true, ""));
    assert(agent_reply_valid("RequestPasskey", false, ""));
    assert(agent_reply_requires_cleanup("RequestPasskey", false, ""));
    assert(agent_reply_requires_cleanup("RequestPasskey", true, "12345"));
    assert(!agent_reply_requires_cleanup("RequestPasskey", true, "012345"));
}
