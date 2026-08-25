#pragma once

#include <functional>
#include <utility>

#include "lvgl_components.hpp"

class LvSettingRollerPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRollerPage3() = default;

    LvSettingRollerPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingRollerPage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

private:
    int initial_selection() const override
    {
        return 0;
    }
};
