#include "screensaver_model.hpp"

#include <algorithm>

namespace {

constexpr int kVelocityX = 45;
constexpr int kVelocityY = 35;

uint32_t elapsed_since(uint32_t now, uint32_t previous)
{
    return now - previous;
}

} // namespace

void ScreensaverModel::reset(uint32_t now)
{
    active_ = false;
    swallow_active_ = false;
    last_activity_tick_ = now;
    last_frame_tick_ = 0;
}

void ScreensaverModel::set_foreground(bool foreground, uint32_t now)
{
    foreground_ = foreground;
    reset(now);
}

bool ScreensaverModel::should_activate(uint32_t now, uint32_t timeout_ms, bool runtime_ready) const
{
    return foreground_ && runtime_ready && !active_ && timeout_ms > 0 &&
           elapsed_since(now, last_activity_tick_) >= timeout_ms;
}

ScreensaverFrame ScreensaverModel::activate(int width, int height, uint32_t now)
{
    x_milli_ = std::max(0, (width - block_size()) / 4) * 1000;
    y_milli_ = std::max(0, (height - block_size()) / 3) * 1000;
    velocity_x_ = kVelocityX;
    velocity_y_ = kVelocityY;
    color_index_ = 0;
    active_ = true;
    last_frame_tick_ = now;
    return frame(true);
}

void ScreensaverModel::deactivate()
{
    active_ = false;
    last_frame_tick_ = 0;
}

ScreensaverFrame ScreensaverModel::advance(int width, int height, uint32_t now)
{
    if (!active_) return frame();

    const uint32_t elapsed = std::min<uint32_t>(elapsed_since(now, last_frame_tick_), 100);
    last_frame_tick_ = now;
    const int32_t max_x = std::max(0, width - block_size()) * 1000;
    const int32_t max_y = std::max(0, height - block_size()) * 1000;
    x_milli_ += velocity_x_ * static_cast<int32_t>(elapsed);
    y_milli_ += velocity_y_ * static_cast<int32_t>(elapsed);

    bool collided = false;
    if (x_milli_ <= 0 || x_milli_ >= max_x) {
        x_milli_ = std::clamp<int32_t>(x_milli_, 0, max_x);
        velocity_x_ = -velocity_x_;
        collided = true;
    }
    if (y_milli_ <= 0 || y_milli_ >= max_y) {
        y_milli_ = std::clamp<int32_t>(y_milli_, 0, max_y);
        velocity_y_ = -velocity_y_;
        collided = true;
    }
    if (collided) color_index_ = (color_index_ + 1) % color_count();
    return frame(collided);
}

bool ScreensaverModel::filter_key(uint32_t key_code, bool released, uint32_t now)
{
    last_activity_tick_ = now;
    if (swallow_active_) {
        if (key_code == swallowed_key_code_) {
            if (released) swallow_active_ = false;
            return true;
        }
        swallow_active_ = false;
    }

    if (!active_) return false;
    deactivate();
    swallowed_key_code_ = key_code;
    swallow_active_ = !released;
    return true;
}

ScreensaverFrame ScreensaverModel::frame(bool color_changed) const
{
    return {x_milli_ / 1000, y_milli_ / 1000, color_index_, color_changed};
}
