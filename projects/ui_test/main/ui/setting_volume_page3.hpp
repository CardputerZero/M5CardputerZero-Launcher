#pragma once

#include <functional>
#include <utility>

#include "lvgl_componens.hpp"

class LvSettingVolumePage3 : public LvSettingValuePage3Base {
public:
    LvSettingVolumePage3() = default;

    LvSettingVolumePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingVolumePage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};
