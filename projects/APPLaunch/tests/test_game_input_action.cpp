#include "../main/ui/model/game_input_action.hpp"

#include <cassert>

int main()
{
    key_item item{};
    item.key_code = KEY_F;
    item.semantic_key = KEY_UP;
    assert(game_input_action(&item) == GameInputAction::UP);

    item.key_code = KEY_X;
    item.semantic_key = KEY_DOWN;
    assert(game_input_action(&item) == GameInputAction::DOWN);
    item.key_code = KEY_Z;
    item.semantic_key = KEY_LEFT;
    assert(game_input_action(&item) == GameInputAction::LEFT);
    item.key_code = KEY_C;
    item.semantic_key = KEY_RIGHT;
    assert(game_input_action(&item) == GameInputAction::RIGHT);

    item = {};
    item.key_code = KEY_SPACE;
    assert(game_input_action(&item) == GameInputAction::FIRE);
    item.key_code = KEY_ENTER;
    assert(game_input_action(&item) == GameInputAction::CONFIRM);
    item.key_code = KEY_ESC;
    assert(game_input_action(&item) == GameInputAction::CANCEL);
    assert(game_input_action(nullptr) == GameInputAction::NONE);
    return 0;
}
