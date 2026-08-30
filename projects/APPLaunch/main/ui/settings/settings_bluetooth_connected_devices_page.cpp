#include "settings_bluetooth_connected_devices_page.hpp"

#include <utility>

LvSettingBluetoothConnectedPage3::LvSettingBluetoothConnectedPage3() = default;

LvSettingBluetoothConnectedPage3::LvSettingBluetoothConnectedPage3(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback)
    : LvSettingBluetoothPage3(parent,
                              parent_node,
                              std::move(back_callback),
                              LvSettingBluetoothListMode::Connected)
{
}
