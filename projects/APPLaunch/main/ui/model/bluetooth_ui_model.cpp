#include "bluetooth_ui_model.hpp"

#include <sstream>
#include <utility>

bool BluetoothStatusSnapshot::decode(const std::string &wire, BluetoothStatusSnapshot &out)
{
    BluetoothWireStatus decoded;
    if (!BluetoothPageModel::decode_status_record(wire, decoded))
        return false;
    BluetoothStatusSnapshot parsed;
    parsed.powered = decoded.powered;
    parsed.address = std::move(decoded.address);
    parsed.discoverable = decoded.discoverable;
    parsed.alias = std::move(decoded.alias);
    parsed.named_only = out.named_only; // preserve the caller's config-driven flag
    out = std::move(parsed);
    return true;
}

BluetoothListSnapshot BluetoothListSnapshot::decode(const std::string &wire)
{
    BluetoothListSnapshot snapshot;
    std::istringstream lines(wire);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty())
            continue;
        BluetoothWireDevice decoded;
        if (!BluetoothPageModel::decode_device_record(line, decoded))
            continue;
        BluetoothDeviceState device;
        device.address = std::move(decoded.address);
        device.name = std::move(decoded.name);
        device.rssi = decoded.rssi;
        device.connected = decoded.connected;
        device.paired = decoded.paired;
        device.trusted = decoded.trusted;
        snapshot.devices.push_back(std::move(device));
    }
    return snapshot;
}

void BluetoothUiModel::set_sub_page(BluetoothSubPage sub_page)
{
    sub_page_ = sub_page;
    page_.set_list_mode(sub_page == BluetoothSubPage::SCAN ? BluetoothListMode::SCAN
                                                           : BluetoothListMode::MANAGED);
    list_ = {};
}

void BluetoothUiModel::apply_status(const BluetoothStatusSnapshot &status)
{
    status_ = status;
    page_.set_discoverable(status.discoverable);
    page_.set_alias(status.alias);
}

void BluetoothUiModel::apply_list(const BluetoothListSnapshot &list)
{
    list_ = list;
    page_.rebuild_rows(list_.devices);
}
