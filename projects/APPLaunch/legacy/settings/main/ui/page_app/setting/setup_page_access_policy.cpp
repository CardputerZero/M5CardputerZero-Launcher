#define APP_PAGE_IMPLEMENTATION_UNIT
#include "setup_page_access.hpp"

namespace setting {

MenuItem *find_menu_item(std::vector<MenuItem> &menus, std::string_view label)
{
    for (auto &menu : menus)
        if (menu.label == label)
            return &menu;
    return nullptr;
}

const MenuItem *find_menu_item(const std::vector<MenuItem> &menus, std::string_view label)
{
    for (const auto &menu : menus)
        if (menu.label == label)
            return &menu;
    return nullptr;
}

MenuItem *selected_menu_item(std::vector<MenuItem> &menus, int selected_index)
{
    if (selected_index < 0 || selected_index >= static_cast<int>(menus.size()))
        return nullptr;
    return &menus[static_cast<std::size_t>(selected_index)];
}

const MenuItem *selected_menu_item(const std::vector<MenuItem> &menus, int selected_index)
{
    if (selected_index < 0 || selected_index >= static_cast<int>(menus.size()))
        return nullptr;
    return &menus[static_cast<std::size_t>(selected_index)];
}

} // namespace setting
