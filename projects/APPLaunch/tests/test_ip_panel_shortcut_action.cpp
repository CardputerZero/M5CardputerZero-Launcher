#include "../main/ui/model/ip_panel_shortcut_action.hpp"

#include <cassert>

int main()
{
    key_item item{};
    item.key_code = KEY_HELP;
    assert(ip_panel_shortcut_action(&item) == IpPanelShortcutAction::SHOW_HELP);

    item = {};
    item.key_code = KEY_H;
    item.mods = KBD_MOD_FN;
    assert(ip_panel_shortcut_action(&item) == IpPanelShortcutAction::NONE);

    item = {};
    item.key_code = KEY_H;
    item.semantic_key = KEY_HELP;
    assert(ip_panel_shortcut_action(&item) == IpPanelShortcutAction::SHOW_HELP);
    assert(ip_panel_shortcut_action(nullptr) == IpPanelShortcutAction::NONE);
    return 0;
}
