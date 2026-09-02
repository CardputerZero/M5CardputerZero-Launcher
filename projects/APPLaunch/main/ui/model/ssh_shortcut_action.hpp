#pragma once

#include "input_keys.h"
#include "keyboard_input.h"

enum class SshShortcutAction {
    NONE,
    SHOW_HELP,
    PREVIOUS_FIELD,
    NEXT_FIELD,
};

inline SshShortcutAction ssh_shortcut_action(const key_item *item)
{
    if (!item) return SshShortcutAction::NONE;
    if (item->key_code == KEY_HELP || item->key_code == KEY_F1)
        return SshShortcutAction::SHOW_HELP;

    if ((item->mods & KBD_MOD_FN) != 0) {
        switch (item->key_code) {
        case KEY_F: return SshShortcutAction::PREVIOUS_FIELD;
        case KEY_X: return SshShortcutAction::NEXT_FIELD;
        default: break;
        }
    }

    switch (item->key_code) {
    case KEY_UP: return SshShortcutAction::PREVIOUS_FIELD;
    case KEY_DOWN: return SshShortcutAction::NEXT_FIELD;
    default: return SshShortcutAction::NONE;
    }
}
