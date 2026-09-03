#pragma once

#include <cstdint>
#include <random>
#include <vector>

enum class TankDirection { UP, DOWN, LEFT, RIGHT };

struct TankBattleTank {
    int x = 0;
    int y = 0;
    TankDirection direction = TankDirection::UP;
    bool alive = true;
    int fire_cooldown = 0;
};

struct TankBattleBullet {
    int x = 0;
    int y = 0;
    TankDirection direction = TankDirection::UP;
    bool from_player = true;
    bool alive = false;
};

class TankBattleModel
{
public:
    static constexpr int GRID_COLUMNS = 18;
    static constexpr int GRID_ROWS = 8;
    static constexpr int ENEMY_COUNT = 5;
    static constexpr int BULLET_CAPACITY = 24;

    explicit TankBattleModel(uint32_t random_seed = 0xC0FFEE);

    void reset();
    bool move_player(TankDirection direction);
    bool player_fire();
    void tick();

    const TankBattleTank &player() const { return player_; }
    const std::vector<TankBattleTank> &enemies() const { return enemies_; }
    const std::vector<TankBattleBullet> &bullets() const { return bullets_; }
    bool game_over() const { return game_over_; }
    bool won() const { return won_; }
    int score() const { return score_; }
    int tick_count() const { return tick_count_; }
    int alive_enemy_count() const;

private:
    bool inside(int x, int y) const;
    bool has_enemy_at(int x, int y, int skip_index = -1) const;
    static void direction_step(TankDirection direction, int &dx, int &dy);
    bool spawn_bullet(int x, int y, TankDirection direction, bool from_player);
    void enemy_fire(TankBattleTank &enemy);
    void update_enemies();
    void move_bullets();
    void update_end_state();

    TankBattleTank player_;
    std::vector<TankBattleTank> enemies_;
    std::vector<TankBattleBullet> bullets_;
    bool game_over_ = false;
    bool won_ = false;
    int score_ = 0;
    int tick_count_ = 0;
    std::minstd_rand random_;
};
