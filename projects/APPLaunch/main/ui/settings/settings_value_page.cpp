#include "settings_value_page.hpp"

#include <utility>

LvSettingRollerPage3::LvSettingRollerPage3() = default;

LvSettingRollerPage3::LvSettingRollerPage3(lv_obj_t *parent, const NodeIter &parent_node)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize(parent);
}

LvSettingRollerPage3::LvSettingRollerPage3(lv_obj_t *parent,
                                           const NodeIter &parent_node,
                                           std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, std::move(back_callback))
{
    initialize(parent);
}

int LvSettingRollerPage3::initial_selection() const
{
    return 0;
}
