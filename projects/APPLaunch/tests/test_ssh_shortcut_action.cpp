#include "../main/ui/model/ssh_shortcut_action.hpp"

#include <cassert>

int main()
{
    key_item item{};
    item.key_code = KEY_H;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::NONE);

    item.mods = KBD_MOD_FN;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::NONE);

    item = {};
    item.key_code = KEY_HELP;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::SHOW_HELP);

    item = {};
    item.mods = KBD_MOD_FN;
    item.key_code = KEY_F;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::PREVIOUS_FIELD);
    item.key_code = KEY_X;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::NEXT_FIELD);

    item = {};
    item.key_code = KEY_F1;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::SHOW_HELP);
    item.key_code = KEY_UP;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::PREVIOUS_FIELD);
    item.key_code = KEY_DOWN;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::NEXT_FIELD);
    item.key_code = KEY_F;
    assert(ssh_shortcut_action(&item) == SshShortcutAction::NONE);
    assert(ssh_shortcut_action(nullptr) == SshShortcutAction::NONE);
}
