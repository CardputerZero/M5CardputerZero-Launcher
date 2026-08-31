#include "settings_bluetooth_scan_page.hpp"

#include <utility>

LvSettingBluetoothScanPage3::LvSettingBluetoothScanPage3() = default;

LvSettingBluetoothScanPage3::LvSettingBluetoothScanPage3(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback)
    : LvSettingBluetoothPage3(parent,
                              parent_node,
                              std::move(back_callback),
                              LvSettingBluetoothListMode::Scan)
{
}
