#include "../main/ui/model/tank_battle_model.hpp"
#include "../main/ui/model/tank_battle_page_contract.hpp"

#include <algorithm>
#include <cassert>

static int active_bullet_count(const TankBattleModel &model)
{
    return static_cast<int>(std::count_if(model.bullets().begin(), model.bullets().end(),
                                          [](const TankBattleBullet &bullet) { return bullet.alive; }));
}

static void assert_same_state(const TankBattleModel &left, const TankBattleModel &right)
{
    assert(left.player().x == right.player().x);
    assert(left.player().y == right.player().y);
    assert(left.player().alive == right.player().alive);
    assert(left.score() == right.score());
    assert(left.game_over() == right.game_over());
    assert(left.won() == right.won());
    assert(left.enemies().size() == right.enemies().size());
    assert(left.bullets().size() == right.bullets().size());
    for (size_t index = 0; index < left.enemies().size(); ++index) {
        assert(left.enemies()[index].x == right.enemies()[index].x);
        assert(left.enemies()[index].y == right.enemies()[index].y);
        assert(left.enemies()[index].alive == right.enemies()[index].alive);
    }
    for (size_t index = 0; index < left.bullets().size(); ++index) {
        assert(left.bullets()[index].x == right.bullets()[index].x);
        assert(left.bullets()[index].y == right.bullets()[index].y);
        assert(left.bullets()[index].alive == right.bullets()[index].alive);
    }
}

int main()
{
    int background = 0;
    int arena = 0;
    int player = 0;
    assert(tank_battle_ui_ready(&background, &arena, &player));
    assert(!tank_battle_ui_ready(nullptr, &arena, &player));
    assert(!tank_battle_ui_ready(&background, nullptr, &player));
    assert(!tank_battle_ui_ready(&background, &arena, nullptr));
    static_assert(noexcept(tank_battle_tick_callback_allowed(
        false, false, static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        static_cast<int *>(nullptr))));
    assert(tank_battle_tick_callback_allowed(
        true, true, &background, &arena, &player));
    assert(!tank_battle_tick_callback_allowed(
        false, true, &background, &arena, &player));
    assert(!tank_battle_tick_callback_allowed(
        true, false, &background, &arena, &player));
    assert(!tank_battle_tick_callback_allowed(
        true, true, &background, &arena, static_cast<int *>(nullptr)));
    int root_screen = 0;
    int stale_root = 0;
    static_assert(noexcept(tank_battle_root_event_callback_allowed(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr))));
    assert(tank_battle_root_event_callback_allowed(&root_screen, &root_screen));
    assert(!tank_battle_root_event_callback_allowed(&stale_root, &root_screen));
    assert(!tank_battle_root_event_callback_allowed(
        static_cast<int *>(nullptr), &root_screen));
    int child = 0;
    static_assert(noexcept(tank_battle_owned_delete_callback_allowed(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr))));
    assert(tank_battle_owned_delete_callback_allowed(&arena, &arena));
    assert(!tank_battle_owned_delete_callback_allowed(&child, &arena));
    assert(!tank_battle_owned_delete_callback_allowed(
        static_cast<int *>(nullptr), &arena));

    TankBattleModel model;
    assert(model.player().x == TankBattleModel::GRID_COLUMNS / 2);
    assert(model.player().y == TankBattleModel::GRID_ROWS - 1);
    assert(model.alive_enemy_count() == TankBattleModel::ENEMY_COUNT);
    assert(model.bullets().size() == TankBattleModel::BULLET_CAPACITY);

    assert(!model.move_player(TankDirection::DOWN));
    for (int column = model.player().x; column > 0; --column)
        assert(model.move_player(TankDirection::LEFT));
    assert(model.player().x == 0);
    assert(!model.move_player(TankDirection::LEFT));
    assert(model.player().direction == TankDirection::LEFT);

    assert(model.move_player(TankDirection::RIGHT));
    assert(model.player_fire());
    assert(!model.player_fire());
    assert(active_bullet_count(model) == 1);
    for (int tick = 0; tick < 4; ++tick) model.tick();
    assert(model.player().fire_cooldown == 0);
    assert(model.player_fire());

    TankBattleModel full_pool_model(1);
    auto &full_pool_bullets = const_cast<std::vector<TankBattleBullet> &>(
        full_pool_model.bullets());
    for (auto &bullet : full_pool_bullets) {
        bullet.x = TankBattleModel::GRID_COLUMNS / 2;
        bullet.y = TankBattleModel::GRID_ROWS / 2;
        bullet.direction = TankDirection::UP;
        bullet.from_player = false;
        bullet.alive = true;
    }
    assert(!full_pool_model.player_fire());
    assert(full_pool_model.player().fire_cooldown == 0);
    full_pool_model.tick();
    assert(full_pool_model.enemies()[0].fire_cooldown == 0);

    TankBattleModel available_pool_model(1);
    available_pool_model.tick();
    assert(available_pool_model.enemies()[0].fire_cooldown > 0);

    model.reset();
    assert(model.score() == 0);
    assert(model.tick_count() == 0);
    assert(!model.game_over());
    assert(active_bullet_count(model) == 0);

    TankBattleModel replay_a(1234);
    TankBattleModel replay_b(1234);
    replay_a.move_player(TankDirection::LEFT);
    replay_b.move_player(TankDirection::LEFT);
    replay_a.player_fire();
    replay_b.player_fire();
    for (int tick = 0; tick < 40; ++tick) {
        replay_a.tick();
        replay_b.tick();
    }
    assert_same_state(replay_a, replay_b);
}
