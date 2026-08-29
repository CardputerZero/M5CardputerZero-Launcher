#pragma once

#include <functional>

#include "settings_bluetooth_page.hpp"

class LvSettingBluetoothScanPage3 : public LvSettingBluetoothPage3 {
public:
    LvSettingBluetoothScanPage3();

    LvSettingBluetoothScanPage3(lv_obj_t *parent,
                                const NodeIter &parent_node,
                                std::function<void()> back_callback);
};
