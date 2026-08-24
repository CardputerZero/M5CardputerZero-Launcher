#include "../main/ui/page_app/setting/setup_page_access.hpp"

#include <cassert>
#include <vector>

int main()
{
    std::vector<setting::MenuItem> menus = {
        {"Screen", {{"Brightness", false, false, {}}}, {}, {}},
        {"Speaker", {{"Volume", false, false, {}}}, {}, {}},
        {"Screen", {{"Duplicate", false, false, {}}}, {}, {}},
    };

    setting::MenuItem *screen = setting::find_menu_item(menus, "Screen");
    assert(screen == &menus[0]);
    assert(screen->sub_items[0].label == "Brightness");
    screen->sub_items[0].toggle_state = true;
    assert(menus[0].sub_items[0].toggle_state);
    assert(setting::find_menu_item(menus, "Missing") == nullptr);

    const auto &const_menus = menus;
    const setting::MenuItem *speaker = setting::find_menu_item(const_menus, "Speaker");
    assert(speaker == &menus[1]);
    assert(speaker->sub_items[0].label == "Volume");

    assert(setting::selected_menu_item(menus, -1) == nullptr);
    assert(setting::selected_menu_item(menus, 3) == nullptr);
    assert(setting::selected_menu_item(menus, 1) == &menus[1]);
    assert(setting::selected_menu_item(const_menus, 2) == &menus[2]);
}
