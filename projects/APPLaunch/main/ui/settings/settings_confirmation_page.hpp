#pragma once

#include <functional>
#include <utility>

#include "lvgl_components.hpp"

class LvSettingConfirmPage3 : public LvSettingValuePage3Base {
public:
    LvSettingConfirmPage3() = default;

    LvSettingConfirmPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingConfirmPage3(lv_obj_t *parent,
                          const NodeIter &parent_node,
                          std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 1;
    }
};
