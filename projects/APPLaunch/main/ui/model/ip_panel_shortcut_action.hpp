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
    const uint32_t key = item->semantic_key ? item->semantic_key : item->key_code;
    if (key == KEY_HELP)
        return IpPanelShortcutAction::SHOW_HELP;
    return IpPanelShortcutAction::NONE;
}
