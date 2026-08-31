#pragma once

#include <functional>

#include "lvgl_components.hpp"

class LvSettingConfirmPage3 : public LvSettingValuePage3Base {
public:
    LvSettingConfirmPage3();

    LvSettingConfirmPage3(lv_obj_t *parent, const NodeIter &parent_node);

    LvSettingConfirmPage3(lv_obj_t *parent,
                          const NodeIter &parent_node,
                          std::function<void()> back_callback);

protected:
    int initial_selection() const override;
};
