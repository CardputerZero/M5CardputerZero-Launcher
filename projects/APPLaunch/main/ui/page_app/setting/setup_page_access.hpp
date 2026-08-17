#pragma once

#include "menu_types.hpp"
#include "../../model/setup_page_model.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class UISetupPage;
typedef struct _lv_obj_t lv_obj_t;

namespace setting {

class Screen;
class Speaker;
class Camera;
class WiFi;
class Info;
class Developer;
class RTC;
class BluetoothUiSession;
class SoundCard;

struct SetupLayout
{
    int screen_width;
    int screen_height;
    int content_height;
    int wifi_title_width;
    int sub_center_x;
    int value_box_width;
    int sc_margin_x;
    int sc_row_x;
    int sc_card_name_width;
    int sc_control_name_x;
    int sc_control_name_width;
    int sc_control_value_x;
    int sc_control_value_width;
    int sc_detail_text_width;
    int sc_input_x;
    int sc_input_width;
    int sc_bottom_hint_width;
};

MenuItem *find_menu_item(std::vector<MenuItem> &menus, std::string_view label);
const MenuItem *find_menu_item(const std::vector<MenuItem> &menus, std::string_view label);
MenuItem *selected_menu_item(std::vector<MenuItem> &menus, int selected_index);
const MenuItem *selected_menu_item(const std::vector<MenuItem> &menus, int selected_index);

class SetupPageAccess
{
public:
    explicit SetupPageAccess(UISetupPage &page) : page_(page) {}

    std::vector<MenuItem> &menus();
    MenuItem *find_menu(std::string_view label);
    MenuItem *selected_menu();

    Screen &screen();
    Speaker &speaker();
    Camera &camera();
    WiFi &wifi();
    Info &info();
    Developer &developer();
    RTC &rtc();
    BluetoothUiSession &bluetooth();
    void release_bluetooth_ui();
    SoundCard &soundcard();

    void confirm(const char *title, std::function<void()> action);
    void enter_value(std::string title, std::vector<std::string> options, int selected_index);
    const std::string &value_title() const;
    int value_selection() const;

    int config_get_int(const char *key, int default_value) const;
    bool config_set_int(const char *key, int value) const;
    bool config_save() const;
    int audio_volume_read() const;
    int audio_volume_write(int value) const;
    bool gpio_set(const char *name, int value) const;
    int gpio_get(const char *name) const;

    void set_view(SetupViewState view);
    void select_sub(int index, int item_count);
    bool enter_help();
    bool leave_help();
    SetupViewState view() const;
    bool is_view(SetupViewState view) const;
    lv_obj_t *content_container();
    int content_height() const;
    SetupLayout layout() const;
    static SetupLayout fixed_layout();
    std::string_view current_utf8() const;
    const std::string &arrow_up_path() const;
    const std::string &arrow_down_path() const;

    void play_enter();
    void play_back();
    void build_main_view();
    void build_sub_view();
    void rebuild_view();
    void transition_enter();
    void transition_back();
    void apply_overflow(lv_obj_t *label, int left, int width, bool focused) const;
    void apply_overflow_centered(lv_obj_t *label, int center, int width, bool focused) const;
    void apply_fixed_label_box(lv_obj_t *label, int x, int y, int width, bool scroll) const;

private:
    UISetupPage &page_;
};

} // namespace setting
