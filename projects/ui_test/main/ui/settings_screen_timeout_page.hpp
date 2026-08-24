#pragma once

#include <functional>
#include <utility>

#include "lvgl_components.hpp"

class LvSettingDarkTimePage3 : public LvSettingValuePage3Base {
public:
    LvSettingDarkTimePage3() = default;

    LvSettingDarkTimePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingDarkTimePage3(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 2;
    }
};
