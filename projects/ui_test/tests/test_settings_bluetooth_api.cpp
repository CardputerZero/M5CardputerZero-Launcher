#include "settings_bluetooth_api.hpp"

#include <cassert>
#include <list>
#include <string>
#include <vector>

namespace {

void test_status_decoder()
{
    settings_bluetooth::StatusRecord status;
    assert(settings_bluetooth::decode_status(
        "1\tAA:bb:01:23:45:67\t0\tCardputerZero", status));
    assert(status.powered);
    assert(status.address == "AA:bb:01:23:45:67");
    assert(!status.discoverable);
    assert(status.alias == "CardputerZero");

    assert(settings_bluetooth::decode_status("0\t\t0\t", status));
    assert(!status.powered);
    assert(status.address.empty());
    assert(status.alias.empty());

    assert(!settings_bluetooth::decode_status(
        "1\tAA:bb:01:23:45:67\t2\tCardputerZero", status));
    assert(!settings_bluetooth::decode_status(
        "1\tAA:bb:01:23:45:67\t0\tbad\tfield", status));
    assert(!settings_bluetooth::decode_status(
        "1\tbad-address\t0\tCardputerZero", status));
    assert(!settings_bluetooth::decode_status(
        "1\tAA:bb:01:23:45:67\t0\tbad\nname", status));
    assert(!settings_bluetooth::decode_status("1\t\t0\tCardputerZero", status));
}

void test_device_decoder()
{
    settings_bluetooth::DeviceRecord device;
    assert(settings_bluetooth::decode_device(
        "AA:bb:01:23:45:67\t-42\t0\t1\t0\tHeadphones", device));
    assert(device.address == "AA:bb:01:23:45:67");
    assert(device.rssi == -42);
    assert(!device.connected);
    assert(device.paired);
    assert(!device.trusted);
    assert(device.name == "Headphones");

    assert(settings_bluetooth::decode_device(
        "AA:bb:01:23:45:67\t0\t0\t0\t0\t", device));
    assert(device.name.empty());
    assert(!settings_bluetooth::decode_device(
        "AA:bb:01:23:45:67\t-42\t0\t1\t0", device));
    assert(!settings_bluetooth::decode_device(
        "AA:bb:01:23:45:67\t-42\t0\t3\t0\tHeadphones", device));
    assert(!settings_bluetooth::decode_device(
        "AA:bb:01:23:45:67\t-42\t0\t1\t0\tHead\tphones", device));
    assert(!settings_bluetooth::decode_device(
        "AA:bb:01:23:45:67\t-42\t0\t1\t0\tbad\rname", device));
}

void test_device_list_decoder()
{
    std::vector<settings_bluetooth::DeviceRecord> devices;
    const std::string payload =
        "AA:bb:01:23:45:67\t-42\t0\t1\t0\tHeadphones\n"
        "11:22:33:44:55:66\t-60\t1\t1\t1\tKeyboard\n";
    assert(settings_bluetooth::decode_devices(payload, devices, 2));
    assert(devices.size() == 2);
    assert(devices[1].connected);

    const std::string single_record =
        "AA:bb:01:23:45:67\t-42\t0\t1\t0\tHeadphones\n";
    assert(settings_bluetooth::decode_devices(single_record, devices, 1));
    assert(devices.size() == 1);
    assert(settings_bluetooth::decode_device_list_reply(1, single_record, devices));

    assert(settings_bluetooth::decode_device_list_reply(2, payload, devices));
    assert(devices.size() == 2);
    assert(!settings_bluetooth::decode_device_list_reply(1, payload, devices));
    assert(!settings_bluetooth::decode_device_list_reply(-1, payload, devices));
    assert(settings_bluetooth::decode_device_list_reply(0, "", devices));
    assert(!settings_bluetooth::decode_device_list_reply(0, payload, devices));

    assert(settings_bluetooth::decode_devices("", devices));
    assert(devices.empty());
    assert(!settings_bluetooth::decode_devices("\n", devices));
    assert(!settings_bluetooth::decode_devices(payload + "\n", devices, 2));
    assert(!settings_bluetooth::decode_devices(
        "AA:bb:01:23:45:67\t-42\t0\t1\t0\tHeadphones\n\n", devices));
    assert(!settings_bluetooth::decode_devices(payload, devices, 1));
}

void test_requests_and_aliases()
{
    std::list<std::string> request;
    assert(settings_bluetooth::alias_request("耳机🎧", request));
    assert(request == std::list<std::string>({"BtAlias", "耳机🎧"}));
    assert(!settings_bluetooth::alias_request("", request));
    assert(!settings_bluetooth::alias_request(std::string(64, 'a'), request));
    assert(!settings_bluetooth::alias_request("bad\x01name", request));
    assert(!settings_bluetooth::alias_request("\xF0\x28\x8C\x28", request));

    assert(settings_bluetooth::power_request(true) ==
           std::list<std::string>({"BtPower", "1"}));
    assert(settings_bluetooth::discoverable_request(false) ==
           std::list<std::string>({"BtDiscoverable", "0"}));
    assert(settings_bluetooth::list_request(false, 16, request));
    assert(request == std::list<std::string>({"BtList", "16"}));
    assert(settings_bluetooth::list_request(true, 16, request));
    assert(request == std::list<std::string>({"BtConnectedList", "16"}));
    assert(!settings_bluetooth::list_request(false, 0, request));
    assert(settings_bluetooth::device_request(
        "BtConnect", "AA:bb:01:23:45:67", request));
    assert(!settings_bluetooth::device_request(
        "BtConnect", "not-an-address", request));
    assert(!settings_bluetooth::device_request(
        "BtPower", "AA:bb:01:23:45:67", request));
    assert(settings_bluetooth::success_without_payload(0, ""));
    assert(settings_bluetooth::success_without_payload(0, "ok"));
    assert(!settings_bluetooth::success_without_payload(0, "failed"));
    assert(!settings_bluetooth::success_without_payload(-1, ""));
}

} // namespace

int main()
{
    test_status_decoder();
    test_device_decoder();
    test_device_list_decoder();
    test_requests_and_aliases();
}
