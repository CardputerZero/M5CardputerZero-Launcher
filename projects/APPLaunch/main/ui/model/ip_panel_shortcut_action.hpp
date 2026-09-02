#pragma once

#include "input_keys.h"
#include "keyboard_input.h"

enum class IpPanelShortcutAction {
    NONE,
    SHOW_HELP,
};

inline IpPanelShortcutAction ip_panel_shortcut_action(const key_item *item)
{
    if (!item) return IpPanelShortcutAction::NONE;
    if (item->key_code == KEY_F1) return IpPanelShortcutAction::SHOW_HELP;
    if ((item->mods & KBD_MOD_FN) != 0 && item->key_code == KEY_H)
        return IpPanelShortcutAction::SHOW_HELP;
    return IpPanelShortcutAction::NONE;
}
