/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_setup.hpp"
#include "../model/setup_value_policy.hpp"

UISetupPage::UISetupPage()
    : AppPage()
{
    set_page_title("Settings");
    cache_image_paths();
    menu_init();
    create_ui();
    if (root_screen_ && ui_obj_["list_cont"]) event_handler_init();
}

UISetupPage::~UISetupPage() noexcept
{
    if (!setup_teardown_step([this] { on_root_deleted(); }))
        lifecycle_.root_deleted();
    setup_teardown_step([this] {
        if (root_screen_)
            lv_obj_remove_event_cb_with_user_data(
                root_screen_, UISetupPage::static_handler, this);
    });
}

void UISetupPage::play_enter()
{
    cp0_signal_audio_api({"SystemSoundPlay", "2"}, nullptr);
}

void UISetupPage::play_back()
{
    cp0_signal_audio_api({"SystemSoundPlay", "1"}, nullptr);
}

void UISetupPage::schedule_volume_preview()
{
    stop_volume_preview_timer();
    volume_preview_timer_ = lv_timer_create(volume_preview_timer_cb, 180, this);
    if (volume_preview_timer_)
        lv_timer_set_repeat_count(volume_preview_timer_, 1);
}

void UISetupPage::stop_volume_preview_timer()
{
    if (!volume_preview_timer_) return;
    lv_timer_delete(volume_preview_timer_);
    volume_preview_timer_ = nullptr;
}

void UISetupPage::volume_preview_timer_cb(lv_timer_t *timer) noexcept
{
    try {
        auto *page = timer ? static_cast<UISetupPage *>(lv_timer_get_user_data(timer)) : nullptr;
        if (!page || timer != page->volume_preview_timer_ || !page->lifecycle_.active())
            return;
        page->volume_preview_timer_ = nullptr;
        page->play_enter();
    } catch (...) {
    }
}

int UISetupPage::config_get_int(const char *key, int default_val)
{
    if (!key || !*key) return default_val;
    int value = default_val;
    cp0_signal_config_api({"GetInt", key ? std::string(key) : std::string(),
                          std::to_string(default_val)},
                          [&](int code, std::string data) {
                              int parsed = 0;
                              if (code == 0 &&
                                  setup_values::parse_nonnegative_int(data, parsed))
                                  value = parsed;
                          });
    return value;
}

bool UISetupPage::config_set_int(const char *key, int val)
{
    if (!key || !*key) return false;
    bool succeeded = false;
    cp0_signal_config_api({"SetInt", std::string(key), std::to_string(val)},
                          [&](int code, std::string) { succeeded = code == 0; });
    return succeeded;
}

bool UISetupPage::config_save()
{
    bool succeeded = false;
    cp0_signal_config_api({"Save"},
                          [&](int code, std::string) { succeeded = code == 0; });
    return succeeded;
}

bool UISetupPage::gpio_set(const char *name, int val)
{
    if (!name || !*name || (val != 0 && val != 1)) return false;
    bool succeeded = false;
    cp0_signal_settings_api({"GpioSet", std::string(name), std::to_string(val)},
                            [&](int code, std::string) { succeeded = code == 0; });
    return succeeded;
}

int UISetupPage::gpio_get(const char *name)
{
    if (!name || !*name) return -1;
    int value = -1;
    cp0_signal_settings_api({"GpioGet", std::string(name)},
                            [&](int code, std::string data) {
                                int parsed = -1;
                                if (code == 0 && setup_values::parse_nonnegative_int(data, parsed) &&
                                    (parsed == 0 || parsed == 1))
                                    value = parsed;
                            });
    return value;
}

int UISetupPage::audio_volume_read()
{
    int volume = -1;
    cp0_signal_audio_api({"VolumeRead"}, [&](int code, std::string data) {
        int parsed = -1;
        if (code == 0 && setup_values::parse_nonnegative_int(data, parsed) &&
            setup_values::volume_value_valid(parsed))
            volume = parsed;
    });
    return volume;
}

int UISetupPage::audio_volume_write(int val)
{
    if (!setup_values::volume_value_valid(val)) return -1;
    int volume = -1;
    cp0_signal_audio_api({"VolumeWrite", std::to_string(val)},
                         [&](int code, std::string data) {
                             int parsed = -1;
                             if (code == 0 && setup_values::parse_nonnegative_int(data, parsed) &&
                                 setup_values::volume_value_valid(parsed))
                                 volume = parsed;
                         });
    return volume;
}

void UISetupPage::play_audio_file(const std::string &path)
{
    if (!path.empty()) cp0_signal_audio_api({"PlayFile", path}, nullptr);
}

void UISetupPage::cache_image_paths()
{
    img_arrow_up_ = launcher_platform::path("setting_red_up.png");
    img_arrow_down_ = launcher_platform::path("setting_red_down.png");
    img_right_arrow_ = launcher_platform::path("setting_right_arrow.png");
    img_ok_ = launcher_platform::path("setting_ok.png");
    img_cross_ = launcher_platform::path("setting_cross.png");
    snd_enter_ = launcher_platform::path("key_enter.wav");
    snd_back_ = launcher_platform::path("key_back.wav");
}

void UISetupPage::menu_init()
{
    setting::build_menu(*this);
}

void UISetupPage::stop_power_timer()
{
    if (!pwr_timer_) return;
    lv_timer_delete(pwr_timer_);
    pwr_timer_ = nullptr;
}

int UISetupPage::find_menu(const char *label)
{
    for (size_t i = 0; i < menu_items_.size(); ++i) {
        if (menu_items_[i].label == label) return static_cast<int>(i);
    }
    return -1;
}

void UISetupPage::enter_confirm_action(const char *title, std::function<void()> action)
{
    confirm_controller_.begin(std::move(action));
    model_.enter_value(title ? title : "", {"Yes", "No"}, 1);
    transition_enter_level();
}

bool UISetupPage::apply_value_selection()
{
    if (val_title_ == "Brightness" || val_title_ == "DarkTime") {
        screen_.apply_value(*this);
    } else if (val_title_ == "Volume") {
        return speaker_.apply_value(*this);
    } else if (val_title_ == "Resolution") {
        camera_.apply_value(*this);
    } else if (val_title_ == "BQ Calib") {
        info_.apply_bq_calibrate(*this);
    } else if (val_title_ == "Reboot?" || val_title_ == "Shutdown?" || val_title_ == "Run Setup?") {
        confirm_controller_.resolve(val_sel_idx_ == 0);
    } else if (confirm_controller_.active()) {
        confirm_controller_.resolve(val_sel_idx_ == 0);
    } else if (val_title_ == "Year" || val_title_ == "Month" || val_title_ == "Day" ||
               val_title_ == "Hour" || val_title_ == "Minute" || val_title_ == "Second") {
        rtc_.apply_value(*this);
    }
    return true;
}
