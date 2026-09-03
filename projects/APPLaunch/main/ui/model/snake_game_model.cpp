#include "snake_game_model.hpp"

#include <algorithm>

SnakeGameModel::SnakeGameModel(uint32_t seed) : random_(seed)
{
    reset();
}

void SnakeGameModel::reset()
{
    const int center_x = GRID_COLS / 2;
    const int center_y = GRID_ROWS / 2;
    snake_ = {{center_x, center_y}, {center_x - 1, center_y}, {center_x - 2, center_y}};
    direction_ = Direction::RIGHT;
    queued_direction_ = Direction::RIGHT;
    score_ = 0;
    spawn_food();
}

void SnakeGameModel::queue_direction(Direction direction)
{
    const bool opposite =
        (direction_ == Direction::UP && direction == Direction::DOWN) ||
        (direction_ == Direction::DOWN && direction == Direction::UP) ||
        (direction_ == Direction::LEFT && direction == Direction::RIGHT) ||
        (direction_ == Direction::RIGHT && direction == Direction::LEFT);
    if (!opposite) queued_direction_ = direction;
}

bool SnakeGameModel::tick()
{
    direction_ = queued_direction_;
    Point head = snake_.front();
    switch (direction_) {
    case Direction::UP: --head.y; break;
    case Direction::DOWN: ++head.y; break;
    case Direction::LEFT: --head.x; break;
    case Direction::RIGHT: ++head.x; break;
    }

    const bool growing = head == food_;
    const bool hits_snake = occupied(head) && (growing || !(head == snake_.back()));
    if (head.x < 0 || head.x >= GRID_COLS || head.y < 0 || head.y >= GRID_ROWS ||
        hits_snake) {
        return false;
    }

    snake_.push_front(head);
    if (growing) {
        score_ += 10;
        spawn_food();
    } else {
        snake_.pop_back();
    }
    return true;
}

bool SnakeGameModel::occupied(Point point) const
{
    return std::find(snake_.begin(), snake_.end(), point) != snake_.end();
}

void SnakeGameModel::spawn_food()
{
    constexpr int cell_count = GRID_COLS * GRID_ROWS;
    for (int attempt = 0; attempt < cell_count * 2; ++attempt) {
        const Point candidate{static_cast<int>(random_() % GRID_COLS),
                              static_cast<int>(random_() % GRID_ROWS)};
        if (!occupied(candidate)) {
            food_ = candidate;
            return;
        }
    }

    for (int y = 0; y < GRID_ROWS; ++y) {
        for (int x = 0; x < GRID_COLS; ++x) {
            if (!occupied({x, y})) {
                food_ = {x, y};
                return;
            }
        }
    }
}
