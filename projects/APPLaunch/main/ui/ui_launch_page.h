/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "launcher_ui_app_page.hpp"
#include "model/launcher_navigation_model.hpp"
#include "model/launcher_startup_contract.hpp"
#include <array>
#include <memory>

class Launch;

class UILaunchPage : public home_base
{
public:
    explicit UILaunchPage(Launch *launch);
    ~UILaunchPage();

    void show_home_screen();
    void load_home_screen();
    void start_startup_gif();
    void create_screen();
    enum LauncherCarouselElement : size_t {
        kCardFarLeft = 0,
        kCardLeft,
        kCardCenter,
        kCardRight,
        kCardFarRight,
        kTitleFarLeft,
        kTitleLeft,
        kTitleCenter,
        kTitleRight,
        kTitleFarRight,
        kPageDot0,
        kPageDot1,
        kPageDot2,
        kPageDot3,
        kPageDot4,
        kLauncherCarouselElementCount,
    };

    void init_input_group();
    static void bind_home_input_group();
    static lv_group_t *home_input_group();
    lv_obj_t *panel(size_t slot);
    lv_obj_t *label(size_t slot);

    void refresh_carousel();
    void update_carousel_slot(size_t slot, const char *title, const char *icon);
    void update_carousel_item(lv_obj_t *panel, lv_obj_t *label, const char *title, const char *icon);
    void launch_selected_app();

protected:
    std::array<lv_obj_t *, kLauncherCarouselElementCount> carousel_elements_ = {};

private:
    struct StartupTimerContext
    {
        UILaunchPage *page = nullptr;
        LauncherStartupGeneration::Token generation = 0;
    };

    void create_app_container(lv_obj_t *parent);
    void switch_left();
    void switch_right();
    void fill_left_entering_slot(lv_obj_t *panel, lv_obj_t *label);
    void fill_right_entering_slot(lv_obj_t *panel, lv_obj_t *label);
    void finish_switch_animation();
    void cancel_switch_animation();
    void handle_home_key(lv_event_t *event);
    void handle_startup_gif_event(lv_event_t *event);
    void play_startup_sound_with_retry();
    void stop_startup_presentation();
    void stop_startup_sound_timer();
    void stop_startup_delay();
    void finish_startup_delay();
    void release_startup_screen(lv_obj_t *&screen);

    void rotate_carousel_left(size_t start, size_t end);
    void rotate_carousel_right(size_t start, size_t end);
    void set_page_dot_selected(size_t element, bool selected);
    void set_carousel_element_clickable(size_t element, bool clickable);
    static void set_panel_icon(lv_obj_t *panel, const char *src);
    static void on_left_arrow_clicked(lv_event_t *event) noexcept;
    static void on_right_arrow_clicked(lv_event_t *event) noexcept;
    static void on_app_clicked(lv_event_t *event) noexcept;
    static void on_home_key(lv_event_t *event) noexcept;
    static void on_startup_gif_event(lv_event_t *event) noexcept;
    static void on_owned_obj_deleted(lv_event_t *event) noexcept;
    static void on_root_deleted(lv_event_t *event) noexcept;
    static void startup_sound_timer_cb(lv_timer_t *timer) noexcept;
    static void startup_delay_timer_cb(lv_timer_t *timer) noexcept;

    Launch *launch_ = nullptr;
    lv_obj_t *startup_gif_ = nullptr;
    lv_obj_t *left_arrow_button_ = nullptr;
    lv_obj_t *right_arrow_button_ = nullptr;
    lv_obj_t *carousel_container_ = nullptr;
    lv_timer_t *startup_sound_timer_ = nullptr;
    lv_timer_t *startup_delay_timer_ = nullptr;
    StartupTimerContext *startup_sound_context_ = nullptr;
    StartupTimerContext *startup_delay_context_ = nullptr;
    lv_obj_t *startup_logo_scr_ = nullptr;
    std::array<char, 256> startup_gif_path_ = {};
    std::array<char, 256> startup_logo_path_ = {};
    bool startup_gif_done_ = false;
    int startup_sound_retry_count_ = 0;
    LauncherNavigationModel navigation_;
    LauncherStartupDelayModel startup_delay_model_;
    LauncherStartupGeneration startup_sound_generation_;
    LauncherStartupGeneration startup_delay_generation_;
    std::shared_ptr<bool> carousel_alive_ = std::make_shared<bool>(true);
    bool home_key_registered_ = false;
    bool active_navigation_moves_next_ = false;
};
