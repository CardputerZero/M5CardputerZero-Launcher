#include "../src/cp0/cp0_keyboard_keymap.h"

#include "input_keys.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const struct cp0_keyboard_keymap_entry *mapped = cp0_keyboard_keymap_lookup(26);
    assert(mapped != NULL);
    assert(strcmp(mapped->sym_name, "exclam") == 0);
    assert(strcmp(mapped->utf8, "!") == 0);
    mapped = cp0_keyboard_keymap_lookup(94);
    assert(mapped != NULL);
    assert(strcmp(mapped->sym_name, "parenright") == 0);
    assert(strcmp(mapped->utf8, ")") == 0);
    assert(cp0_keyboard_keymap_lookup(54) == NULL);
    assert(cp0_keyboard_keymap_lookup(9999) == NULL);

    assert(strcmp(cp0_keyboard_control_utf8(KEY_ENTER), "\r") == 0);
    assert(strcmp(cp0_keyboard_control_utf8(KEY_LEFT), "\033[D") == 0);
    assert(strcmp(cp0_keyboard_control_utf8(KEY_F12), "\033[24~") == 0);
    assert(cp0_keyboard_control_utf8(KEY_A) == NULL);
    return 0;
}
