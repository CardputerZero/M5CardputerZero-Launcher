#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace setting {

struct SubItem
{
    std::string label;
    bool is_toggle;
    bool toggle_state;
    std::function<void()> action;
};

struct MenuItem
{
    std::string label;
    std::vector<SubItem> sub_items;
    std::function<void()> on_enter;
    std::function<void(uint32_t key)> custom_key_handler;
};

} // namespace setting
