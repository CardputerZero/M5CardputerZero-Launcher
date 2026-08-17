#include "cp0_bluetooth_api_contract.hpp"

#include <cassert>
#include <stdexcept>

int main()
{
    cp0::bluetooth::Request request;
    assert(cp0::bluetooth::parse_request({"BtStatus"}, request));
    assert(!cp0::bluetooth::parse_request({"BtStatus", "junk"}, request));
    assert(cp0::bluetooth::parse_request({"BtPower", "0"}, request));
    assert(request.value == 0);
    assert(cp0::bluetooth::parse_request({"BtDiscoverable", "1"}, request));
    assert(request.value == 1);
    assert(!cp0::bluetooth::parse_request({"BtPower"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPower", "junk"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPower", "-1"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPower", "2"}, request));

    assert(cp0::bluetooth::parse_request({"BtScan"}, request));
    assert(request.max_count == 16);
    assert(cp0::bluetooth::parse_request({"BtList", "1"}, request));
    assert(request.max_count == 1);
    assert(cp0::bluetooth::parse_request({"BtConnectedList", "16"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "0"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "-1"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "17"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "999999999999"}, request));
    assert(!cp0::bluetooth::parse_request({"BtScan", "2junk"}, request));

    assert(cp0::bluetooth::parse_request({"BtAlias", "Cardputer"}, request));
    assert(request.text == "Cardputer");
    assert(!cp0::bluetooth::parse_request({"BtAlias"}, request));
    assert(!cp0::bluetooth::parse_request({"BtAlias", ""}, request));
    assert(!cp0::bluetooth::parse_request({"BtAlias", "bad\talias"}, request));
    assert(!cp0::bluetooth::parse_request(
        {"BtAlias", std::string("bad\0alias", 9)}, request));
    assert(!cp0::bluetooth::parse_request({"BtAlias", std::string(64, 'a')}, request));
    assert(cp0::bluetooth::parse_request({"BtAlias", std::string(63, 'a')}, request));

    const char *address = "AA:bb:01:23:45:67";
    assert(cp0::bluetooth::parse_request({"BtPair", address}, request));
    assert(cp0::bluetooth::parse_request({"BtConnect", address}, request));
    assert(cp0::bluetooth::parse_request({"BtDisconnect", address}, request));
    assert(cp0::bluetooth::parse_request({"BtRemove", address}, request));
    assert(!cp0::bluetooth::parse_request({"BtPair"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPair", "not-an-address"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPair", address, "junk"}, request));

    // Session lifecycle commands.
    assert(cp0::bluetooth::parse_request({"BtSessionInit"}, request));
    assert(request.command == cp0::bluetooth::Command::SessionInit);
    assert(!cp0::bluetooth::parse_request({"BtSessionInit", "1"}, request));

    assert(cp0::bluetooth::parse_request({"BtSessionDeinit", "42"}, request));
    assert(request.command == cp0::bluetooth::Command::SessionDeinit);
    assert(request.has_session && request.session_id == "42");
    assert(!cp0::bluetooth::parse_request({"BtSessionDeinit"}, request));
    assert(!cp0::bluetooth::parse_request({"BtSessionDeinit", "not-a-number"}, request));

    assert(cp0::bluetooth::parse_request({"BtStatusGet", "7"}, request));
    assert(request.command == cp0::bluetooth::Command::StatusGet);
    assert(request.session_id == "7");
    assert(!cp0::bluetooth::parse_request({"BtStatusGet"}, request));
    assert(!cp0::bluetooth::parse_request({"BtStatusGet", "7", "junk"}, request));

    assert(cp0::bluetooth::parse_request({"BtConnectedListInit", "3"}, request));
    assert(request.command == cp0::bluetooth::Command::ConnectedListInit);
    assert(cp0::bluetooth::parse_request({"BtConnectedListGet", "3"}, request));
    assert(request.command == cp0::bluetooth::Command::ConnectedListGet);
    assert(cp0::bluetooth::parse_request({"BtConnectedListDeinit", "3"}, request));
    assert(request.command == cp0::bluetooth::Command::ConnectedListDeinit);
    assert(cp0::bluetooth::parse_request({"BtScanOn", "3"}, request));
    assert(request.command == cp0::bluetooth::Command::ScanOn);
    assert(cp0::bluetooth::parse_request({"BtScanOff", "3"}, request));
    assert(request.command == cp0::bluetooth::Command::ScanOff);
    assert(!cp0::bluetooth::parse_request({"BtScanOn", "3", "junk"}, request));

    // Session-scoped setters / device commands.
    assert(cp0::bluetooth::parse_request({"BtPower", "5", "1"}, request));
    assert(request.command == cp0::bluetooth::Command::Power);
    assert(request.has_session && request.session_id == "5" && request.value == 1);
    assert(cp0::bluetooth::parse_request({"BtDiscoverable", "5", "0"}, request));
    assert(request.value == 0);
    assert(cp0::bluetooth::parse_request({"BtAlias", "5", "NewName"}, request));
    assert(request.text == "NewName");
    assert(cp0::bluetooth::parse_request({"BtPair", "5", address}, request));
    assert(request.command == cp0::bluetooth::Command::Pair);
    assert(request.session_id == "5");
    assert(!cp0::bluetooth::parse_request({"BtPower", "5"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPower", "5", "2"}, request));
    assert(!cp0::bluetooth::parse_request({"BtPair", "5", "bad"}, request));

    assert(cp0::bluetooth::valid_session_id("0"));
    assert(cp0::bluetooth::valid_session_id("12345678901234567890"));
    assert(!cp0::bluetooth::valid_session_id(""));
    assert(!cp0::bluetooth::valid_session_id("-1"));
    assert(!cp0::bluetooth::valid_session_id("12a"));
    assert(!cp0::bluetooth::valid_session_id("123456789012345678901"));

    cp0_bt_status_t encoded_status{};
    encoded_status.powered = 1;
    encoded_status.discoverable = 0;
    std::snprintf(encoded_status.address, sizeof(encoded_status.address), "AA:BB:CC:DD:EE:FF");
    std::snprintf(encoded_status.alias, sizeof(encoded_status.alias), "Cardputer");
    assert(cp0::bluetooth::encode_status(encoded_status) ==
           "1\tAA:BB:CC:DD:EE:FF\t0\tCardputer");

    cp0_bt_device_t device{};
    std::snprintf(device.address, sizeof(device.address), "11:22:33:44:55:66");
    std::snprintf(device.name, sizeof(device.name), "Speaker");
    device.rssi = -30;
    device.connected = 1;
    device.paired = 0;
    device.trusted = 1;
    assert(cp0::bluetooth::encode_devices(&device, 1) ==
           "11:22:33:44:55:66\t-30\t1\t0\t1\tSpeaker\n");

    assert(cp0::bluetooth::sanitize_wire_field("normal") == "normal");
    assert(cp0::bluetooth::sanitize_wire_field(
               std::string("name\twith\rcontrols\n\0", 20)) ==
           std::string("name with controls  ", 20));

    int calls = 0;
    cp0::bluetooth::invoke_callback([&](int code, std::string data) {
        ++calls;
        assert(code == 3 && data == "ok");
        throw std::runtime_error("callback");
    }, 3, "ok");
    assert(calls == 1);

    calls = 0;
    cp0::bluetooth::invoke_backend([&](int code, std::string data) {
        ++calls;
        assert(code == -1 && data == "bluetooth backend failure");
    }, []() -> cp0::bluetooth::Reply { throw std::runtime_error("backend"); });
    assert(calls == 1);

    calls = 0;
    cp0::bluetooth::invoke_backend([&](int code, std::string data) {
        ++calls;
        assert(code == 7 && data == "done");
    }, [] { return cp0::bluetooth::Reply{7, "done"}; });
    assert(calls == 1);
}
