#pragma once

#include "menu_types.hpp"

#include <string>
#include <vector>

class UISetupPage;

namespace setting {

class Launcher
{
public:
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);
    static void save_app_toggle(UISetupPage &page, const std::string &config_key);
};

class Boot
{
public:
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);
    static void factory_reset();
    static void rearm_oobe_and_reboot();
};

class Screen
{
public:
    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void enter_brightness_adjust(UISetupPage &page);
    void enter_darktime_adjust(UISetupPage &page);
    void apply_value(UISetupPage &page);
    static int backlight_read();
    static int backlight_max();

private:
    int bright_val_ = 75;
};

class Speaker
{
public:
    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void enter_volume_adjust(UISetupPage &page);
    bool preview_value(UISetupPage &page);
    void cancel_preview(UISetupPage &page);
    bool apply_value(UISetupPage &page);

private:
    int vol_val_ = 39;
    bool preview_active_ = false;
};

class Camera
{
public:
    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void enter_resolution(UISetupPage &page);
    void apply_value(UISetupPage &page);
};

} // namespace setting
