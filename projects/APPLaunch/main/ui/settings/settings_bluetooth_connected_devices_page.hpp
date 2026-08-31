#pragma once

#include <functional>

#include "settings_bluetooth_page.hpp"

class LvSettingBluetoothConnectedPage3 : public LvSettingBluetoothPage3 {
public:
    LvSettingBluetoothConnectedPage3();

    LvSettingBluetoothConnectedPage3(lv_obj_t *parent,
                                     const NodeIter &parent_node,
                                     std::function<void()> back_callback);
};
