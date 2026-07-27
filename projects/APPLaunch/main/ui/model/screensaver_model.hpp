#pragma once

#include <cstddef>
#include <cstdint>

struct ScreensaverFrame
{
    int x = 0;
    int y = 0;
    size_t color_index = 0;
    bool color_changed = false;
};

class ScreensaverModel
{
public:
    static constexpr int BLOCK_SIZE = 40;
    static constexpr size_t COLOR_COUNT = 8;

    void reset(uint32_t now);
    void set_foreground(bool foreground, uint32_t now);
    bool should_activate(uint32_t now, uint32_t timeout_ms, bool runtime_ready) const;
    ScreensaverFrame activate(int width, int height, uint32_t now);
    void deactivate();
    ScreensaverFrame advance(int width, int height, uint32_t now);
    bool filter_key(uint32_t key_code, bool released, uint32_t now);

    bool active() const { return active_; }
    bool foreground() const { return foreground_; }
    uint32_t last_activity_tick() const { return last_activity_tick_; }

private:
    ScreensaverFrame frame(bool color_changed = false) const;

    uint32_t last_activity_tick_ = 0;
    uint32_t last_frame_tick_ = 0;
    uint32_t swallowed_key_code_ = 0;
    int32_t x_milli_ = 0;
    int32_t y_milli_ = 0;
    int velocity_x_ = 45;
    int velocity_y_ = 35;
    size_t color_index_ = 0;
    bool active_ = false;
    bool foreground_ = true;
    bool swallow_active_ = false;
};
