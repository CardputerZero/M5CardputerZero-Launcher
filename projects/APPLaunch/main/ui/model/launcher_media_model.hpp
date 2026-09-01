#pragma once

#include <cstdint>
#include <string>

class LauncherMediaControlsModel
{
public:
    static bool parse_int(const std::string &text, int &value);
    static bool parse_percent(const std::string &text, int &value);
    static int clamp_percent(int value);
    static int percent_from_raw(int raw, int maximum);
    static int raw_from_percent(int percent, int maximum);

    int volume_or(int fallback) const;
    int brightness_or(int fallback) const;
    bool has_volume() const { return volume_ >= 0; }
    bool has_brightness() const { return brightness_ >= 0; }
    bool muted() const { return muted_; }
    void set_volume(int percent);
    void set_brightness(int percent);
    void set_brightness_from_raw(int raw, int maximum);
    bool toggle_mute();
    void set_mute(bool muted);

private:
    int volume_ = -1;
    int brightness_ = -1;
    bool muted_ = false;
};

enum class LauncherMediaOsdMode
{
    LEVEL,
    MUTE,
};

struct LauncherMediaOsdState
{
    LauncherMediaOsdMode mode = LauncherMediaOsdMode::LEVEL;
    std::string title;
    std::string icon;
    int percent = 0;
    bool visible = false;
};

class LauncherMediaOsdModel
{
public:
    static constexpr uint32_t SHOW_DURATION_MS = 1000;

    void show_level(std::string title, std::string icon, int percent);
    void show_mute(bool muted, std::string mute_icon, std::string volume_icon);
    void hide();

    const LauncherMediaOsdState &state() const { return state_; }

private:
    LauncherMediaOsdState state_;
};
