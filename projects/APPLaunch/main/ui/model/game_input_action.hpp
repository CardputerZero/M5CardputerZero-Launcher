#pragma once

#include "input_keys.h"
#include "keyboard_input.h"

enum class GameInputAction {
    NONE,
    UP,
    DOWN,
    LEFT,
    RIGHT,
    FIRE,
    CONFIRM,
    CANCEL,
};

inline GameInputAction game_input_action(const key_item *item)
{
    if (!item)
        return GameInputAction::NONE;
    const uint32_t key = item->semantic_key ? item->semantic_key : item->key_code;
    switch (key) {
    case KEY_UP: return GameInputAction::UP;
    case KEY_DOWN: return GameInputAction::DOWN;
    case KEY_LEFT: return GameInputAction::LEFT;
    case KEY_RIGHT: return GameInputAction::RIGHT;
    case KEY_SPACE: return GameInputAction::FIRE;
    case KEY_ENTER: return GameInputAction::CONFIRM;
    case KEY_ESC: return GameInputAction::CANCEL;
    default: return GameInputAction::NONE;
    }
}
