#pragma once

#include <functional>
#include <utility>

#include "setting_bluetooth_page.hpp"

class LvSettingBluetoothScanPage3 : public LvSettingBluetoothPage3 {
public:
    LvSettingBluetoothScanPage3() = default;

    LvSettingBluetoothScanPage3(lv_obj_t *parent,
                                const NodeIter &parent_node,
                                std::function<void()> back_callback)
        : LvSettingBluetoothPage3(parent,
                                   parent_node,
                                   std::move(back_callback),
                                   LvSettingBluetoothListMode::Scan)
    {
    }
};
