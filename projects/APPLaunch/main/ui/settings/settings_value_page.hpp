#pragma once

#include <functional>
#include "lvgl_components.hpp"

class LvSettingRollerPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRollerPage3();

    LvSettingRollerPage3(lv_obj_t *parent, const NodeIter &parent_node);

    LvSettingRollerPage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback);

private:
    int initial_selection() const override;
};
