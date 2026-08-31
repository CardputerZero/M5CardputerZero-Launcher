#include "settings_confirmation_page.hpp"

#include <utility>

LvSettingConfirmPage3::LvSettingConfirmPage3() = default;

LvSettingConfirmPage3::LvSettingConfirmPage3(lv_obj_t *parent, const NodeIter &parent_node)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize(parent);
}

LvSettingConfirmPage3::LvSettingConfirmPage3(lv_obj_t *parent,
                                             const NodeIter &parent_node,
                                             std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, std::move(back_callback))
{
    initialize(parent);
}

int LvSettingConfirmPage3::initial_selection() const
{
    return 1;
}
