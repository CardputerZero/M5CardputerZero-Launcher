#include "tank_battle_model.hpp"

#include <algorithm>

TankBattleModel::TankBattleModel(uint32_t random_seed) : random_(random_seed)
{
    reset();
}

void TankBattleModel::reset()
{
    game_over_ = false;
    won_ = false;
    score_ = 0;
    tick_count_ = 0;

    player_ = {};
    player_.x = GRID_COLUMNS / 2;
    player_.y = GRID_ROWS - 1;

    enemies_.clear();
    for (int index = 0; index < ENEMY_COUNT; ++index) {
        TankBattleTank enemy;
        enemy.x = 2 + index * 3;
        enemy.direction = TankDirection::DOWN;
        enemy.fire_cooldown = index * 4;
        enemies_.push_back(enemy);
    }

    bullets_.assign(BULLET_CAPACITY, {});
}

bool TankBattleModel::move_player(TankDirection direction)
{
    if (game_over_) return false;
    player_.direction = direction;
    int dx = 0;
    int dy = 0;
    direction_step(direction, dx, dy);
    const int next_x = player_.x + dx;
    const int next_y = player_.y + dy;
    if (!inside(next_x, next_y) || has_enemy_at(next_x, next_y)) return false;
    player_.x = next_x;
    player_.y = next_y;
    return true;
}

bool TankBattleModel::player_fire()
{
    if (game_over_ || player_.fire_cooldown > 0) return false;
    int dx = 0;
    int dy = 0;
    direction_step(player_.direction, dx, dy);
    const int x = player_.x + dx;
    const int y = player_.y + dy;
    if (!inside(x, y)) return false;
    if (!spawn_bullet(x, y, player_.direction, true)) return false;
    player_.fire_cooldown = 4;
    return true;
}

void TankBattleModel::tick()
{
    if (game_over_) return;
    ++tick_count_;
    if (player_.fire_cooldown > 0) --player_.fire_cooldown;
    for (auto &enemy : enemies_) {
        if (enemy.alive && enemy.fire_cooldown > 0) --enemy.fire_cooldown;
    }
    update_enemies();
    move_bullets();
    update_end_state();
}

int TankBattleModel::alive_enemy_count() const
{
    return static_cast<int>(std::count_if(enemies_.begin(), enemies_.end(),
                                          [](const TankBattleTank &enemy) { return enemy.alive; }));
}

bool TankBattleModel::inside(int x, int y) const
{
    return x >= 0 && x < GRID_COLUMNS && y >= 0 && y < GRID_ROWS;
}

bool TankBattleModel::has_enemy_at(int x, int y, int skip_index) const
{
    for (size_t index = 0; index < enemies_.size(); ++index) {
        if (static_cast<int>(index) == skip_index) continue;
        const auto &enemy = enemies_[index];
        if (enemy.alive && enemy.x == x && enemy.y == y) return true;
    }
    return false;
}

void TankBattleModel::direction_step(TankDirection direction, int &dx, int &dy)
{
    dx = 0;
    dy = 0;
    switch (direction) {
    case TankDirection::UP: dy = -1; break;
    case TankDirection::DOWN: dy = 1; break;
    case TankDirection::LEFT: dx = -1; break;
    case TankDirection::RIGHT: dx = 1; break;
    }
}

bool TankBattleModel::spawn_bullet(int x, int y, TankDirection direction, bool from_player)
{
    for (auto &bullet : bullets_) {
        if (bullet.alive) continue;
        bullet = {x, y, direction, from_player, true};
        return true;
    }
    return false;
}

void TankBattleModel::enemy_fire(TankBattleTank &enemy)
{
    if (!enemy.alive || enemy.fire_cooldown > 0) return;
    int dx = 0;
    int dy = 0;
    direction_step(enemy.direction, dx, dy);
    const int x = enemy.x + dx;
    const int y = enemy.y + dy;
    if (!inside(x, y)) return;
    if (!spawn_bullet(x, y, enemy.direction, false)) return;
    enemy.fire_cooldown = 8 + static_cast<int>(random_() % 8);
}

void TankBattleModel::update_enemies()
{
    for (size_t index = 0; index < enemies_.size(); ++index) {
        auto &enemy = enemies_[index];
        if (!enemy.alive) continue;
        if ((tick_count_ + static_cast<int>(index)) % 6 == 0) {
            const uint32_t next_direction = random_() % 5;
            if (next_direction == 0) enemy.direction = TankDirection::LEFT;
            else if (next_direction == 1) enemy.direction = TankDirection::RIGHT;
            else if (next_direction == 2) enemy.direction = TankDirection::DOWN;
            else if (next_direction == 3) enemy.direction = TankDirection::UP;

            int dx = 0;
            int dy = 0;
            direction_step(enemy.direction, dx, dy);
            const int next_x = enemy.x + dx;
            const int next_y = enemy.y + dy;
            if (inside(next_x, next_y) && !has_enemy_at(next_x, next_y, static_cast<int>(index)) &&
                !(player_.x == next_x && player_.y == next_y)) {
                enemy.x = next_x;
                enemy.y = next_y;
            }
        }
        if ((random_() % 10) < 2) enemy_fire(enemy);
    }
}

void TankBattleModel::move_bullets()
{
    for (auto &bullet : bullets_) {
        if (!bullet.alive) continue;
        int dx = 0;
        int dy = 0;
        direction_step(bullet.direction, dx, dy);
        bullet.x += dx;
        bullet.y += dy;
        if (!inside(bullet.x, bullet.y)) {
            bullet.alive = false;
            continue;
        }
        if (bullet.from_player) {
            for (auto &enemy : enemies_) {
                if (enemy.alive && enemy.x == bullet.x && enemy.y == bullet.y) {
                    enemy.alive = false;
                    bullet.alive = false;
                    score_ += 100;
                    break;
                }
            }
        } else if (player_.alive && player_.x == bullet.x && player_.y == bullet.y) {
            player_.alive = false;
            bullet.alive = false;
            game_over_ = true;
            won_ = false;
        }
    }
}

void TankBattleModel::update_end_state()
{
    if (game_over_) return;
    if (!player_.alive) {
        game_over_ = true;
        won_ = false;
    } else if (alive_enemy_count() == 0) {
        game_over_ = true;
        won_ = true;
    }
}
