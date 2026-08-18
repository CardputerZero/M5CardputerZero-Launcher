/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include "cp0_keyboard_input_context.hpp"
#include "setting/basic.hpp"
#include "../model/rtc_state_model.hpp"
#include "../model/adb_state.hpp"
#include "setting/bluetooth.hpp"
#include "setting/developer.hpp"
#include "setting/info.hpp"
#include "setting/menu_types.hpp"
#include "setting/rtc.hpp"
#include "setting/sound_card.hpp"
#include "setting/system.hpp"
#include "setting/wifi.hpp"
// Keep this page platform-neutral: system and hardware operations go through
// cp0_lvgl's cp0_* service interfaces so the SDL build can compile the page.
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
#ifndef LAUNCHER_GIT_COMMIT_RAW
#define LAUNCHER_GIT_COMMIT_RAW unknown
#endif
#define LAUNCHER_GIT_COMMIT STRINGIFY(LAUNCHER_GIT_COMMIT_RAW)
#ifndef LAUNCHER_VERSION_RAW
#define LAUNCHER_VERSION_RAW 0.0.0-dev
#endif
#ifndef LAUNCHER_CHANNEL_RAW
#define LAUNCHER_CHANNEL_RAW development
#endif
#ifndef LAUNCHER_BUILD_DATE_RAW
#define LAUNCHER_BUILD_DATE_RAW unknown
#endif
#define LAUNCHER_VERSION STRINGIFY(LAUNCHER_VERSION_RAW)
#define LAUNCHER_CHANNEL STRINGIFY(LAUNCHER_CHANNEL_RAW)
#define LAUNCHER_BUILD_DATE STRINGIFY(LAUNCHER_BUILD_DATE_RAW)
#include "../launcher_ui_app_page.hpp"
#include "../model/setup_page_model.hpp"
#include <climits>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <sstream>
#include <thread>
#include <list>
#include <memory>
#include <utility>
#include "cp0_lvgl_app.h"
#include "cp0_async_testable_utils.hpp"
#include "hal_lvgl_bsp.h"
#include "launcher_platform.hpp"
#include "../app_registry.h"


class UISetupPage;
namespace setting { class SetupPageAccess; class Update; }

// ============================================================
//  System settings screen  UISetupPage  (Carousel Design)
//  Screen: 320x170 (top bar20px, body 320x150)
//
//  Menu items (design mockup): Launcher, Boot, Screen, WiFi, Speaker, Camera
//  Actual HAL integration: WiFi scan/connect, brightness, volume, power, reboot, about
// ============================================================

class UISetupPage : public AppPage
{
    using ViewState = SetupViewState;

    using SubItem = setting::SubItem;
    using MenuItem = setting::MenuItem;

public:
    UISetupPage();
    ~UISetupPage() noexcept;

private:
    std::vector<MenuItem> menu_items_;
    friend class setting::SetupPageAccess;
    friend class setting::Update;

    setting::Screen screen_;
    setting::WiFi wifi_;
    setting::Speaker speaker_;
    setting::Camera camera_;
    setting::Info info_;
    setting::Developer developer_;
    setting::RTC rtc_;
    std::unique_ptr<setting::BluetoothUiSession> bluetooth_ui_;
    setting::SoundCard soundcard_;

    setting::BluetoothUiSession &ensure_bluetooth_ui();
    void release_bluetooth_ui();

    SetupPageModel model_;
    Cp0KeyboardInputContextScope input_context_scope_;
    SetupPageLifecycle lifecycle_;
    int &selected_idx_ = model_.selected_index;
    int &sub_selected_idx_ = model_.sub_selected_index;
    ViewState &view_state_ = model_.view;
    std::unordered_map<std::string, lv_obj_t *> ui_obj_;

    // Image paths
    std::string img_arrow_up_;
    std::string img_arrow_down_;
    std::string img_right_arrow_;
    std::string img_ok_;
    std::string img_cross_;

    struct key_item *cur_elm_ = nullptr;

    // Value select (3rd level)
    int &val_sel_idx_ = model_.value_selected_index;
    std::vector<std::string> &val_options_ = model_.value_options;
    std::string &val_title_ = model_.value_title;

    // Power timer
    lv_timer_t *pwr_timer_ = nullptr;
    lv_timer_t *update_timer_ = nullptr;
    lv_timer_t *volume_preview_timer_ = nullptr;
    std::string update_job_id_;

    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 150;
    static constexpr int LIST_H   = SCREEN_H;
    static constexpr int ROWS_VISIBLE = 7;
    static constexpr int ROW_CENTER   = 3;

    // Audio feedback paths
    std::string snd_enter_;
    std::string snd_back_;

    void play_enter();
    void play_back();
    static int config_get_int(const char *key, int default_val);
    static bool config_set_int(const char *key, int val);
    static bool config_save();
    static bool gpio_set(const char *name, int val);
    static int gpio_get(const char *name);
    static int audio_volume_read();
    static int audio_volume_write(int val);
    void play_audio_file(const std::string &path);
    void cache_image_paths();

    // ==================== Menu init ====================
    void menu_init();

    int find_menu(const char *label);

    // ==================== Confirm action (Reboot/Shutdown) ====================
    SetupConfirmController confirm_controller_;

    void enter_confirm_action(const char *title, std::function<void()> action);
    bool apply_value_selection();
    void schedule_volume_preview();
    void stop_volume_preview_timer();
    static void volume_preview_timer_cb(lv_timer_t *timer) noexcept;

    // ==================== Power timer ====================
    void stop_power_timer();
    void start_update_job(const char *command, int item_index);
    void stop_update_timer(bool cancel_job = true);
    static void update_timer_cb(lv_timer_t *timer) noexcept;

    // ==================== UI ====================
    void create_ui();

    // ==================== Animation config ====================
    static constexpr int ANIM_TIME = 200;
    lv_obj_t *row_labels_[ROWS_VISIBLE] = {};
    lv_obj_t *sel_bg_ = nullptr;
    lv_obj_t *hint_lbl_ = nullptr;
    lv_obj_t *arrow_up_obj_ = nullptr;
    lv_obj_t *arrow_down_obj_ = nullptr;

    int row_h() const;
    int row_y(int vi) const;

    struct RowStyle {
        const lv_font_t *font;
        uint32_t color;
        int x;
        int opa;
    };

    static constexpr int MENU_X = 60;

    // Right-column value box for sub / value views. After measuring a value's actual
    // width, anything wider than VALUE_BOX_W is shrunk to VALUE_BOX_W and marquee-scrolled
    // on the focused row / ellipsized elsewhere — so long values (MAC, Commit, IP,
    // hostname, version, ...) scroll instead of overflowing or overlapping. (#57)
    static constexpr int VALUE_BOX_LEFT = 124;
    static constexpr int VALUE_BOX_W    = 88; // 超过 88px 即缩宽并滚动
    // Right-column hint (ok:xxx) scroll threshold. The hint sits at the far right,
    // to the right of any toggle indicator (x=220). Anything wider than this is
    // clamped into a right-edge box and marquee-scrolled. Slightly smaller than the
    // center VALUE_BOX_W so it clears the toggle indicator instead of overlapping it.
    static constexpr int RIGHT_HINT_BOX_W = 74;

    // Width of the "Connected WiFi: <ssid> <ip>" header box in the WiFi list. When the
    // text is wider than this it marquee-scrolls instead of overflowing off-screen (#66).
    static constexpr int WIFI_TITLE_BOX_W = 300;

    // SoundCard uses fixed slots so long ALSA names/values cannot push neighboring
    // text or hints out of place.
    static constexpr int SC_MARGIN_X      = 8;
    static constexpr int SC_ROW_X         = 12;
    static constexpr int SC_CARD_NAME_W   = SCREEN_W - 24;
    static constexpr int SC_CTRL_NAME_X   = 12;
    static constexpr int SC_CTRL_NAME_W   = 172;
    static constexpr int SC_CTRL_VALUE_X  = 190;
    static constexpr int SC_CTRL_VALUE_W  = SCREEN_W - SC_CTRL_VALUE_X - 8;
    static constexpr int SC_DETAIL_TEXT_W = SCREEN_W - 16;
    static constexpr int SC_INPUT_X       = 100;
    static constexpr int SC_INPUT_W       = SCREEN_W - SC_INPUT_X - 12;
    static constexpr int SC_BOTTOM_HINT_W = SCREEN_W - 16;

    static constexpr int SUB_LEFT_BOX_X   = 4;
    static constexpr int SUB_LEFT_BOX_W   = 90;
    static constexpr int SUB_ARROW_X      = 100;
    static constexpr int SUB_CENTER_X     = 160;

    RowStyle style_for_slot(int vi);

    // ==================== Shared: create a styled carousel label ====================
    // vi = visual slot (0..ROWS_VISIBLE-1), center_vi = which slot is "selected"
    // center_x = the pixel X where text center aligns
    // text = label string, hide if empty
    // smaller = true for sub-menu columns (one font size smaller)
    lv_obj_t *create_carousel_label(lv_obj_t *parent, int vi, int center_vi,
                                     const char *text, int center_x, bool smaller = false);

    static constexpr int SUB_RIGHT_ARROW_X = SUB_ARROW_X;
    static constexpr int ARROW_SRC = 19;    // setting_right_arrow.png is 19x19
    static constexpr int ARROW_SCALE = 224; // 256 = 100%; shrink the blue arrow a touch

    // Place the blue "drill-in" arrow between the left column and the right
    // column. It sits just left of the right text with a guaranteed gap, is
    // scaled down slightly, and is drawn *behind* the row text (but above the
    // highlight bar) so a wide left label can never be occluded by it. (plan A)
    void place_blue_arrow(lv_obj_t *parent, lv_obj_t *left_lbl, int right_min_x);
    void place_fixed_sub_arrow(lv_obj_t *parent);

    // Convenience: create a main-menu label at slot vi
    lv_obj_t *create_menu_label(lv_obj_t *parent, int vi, int item_idx, int count);

    // If a (carousel) label's text is wider than box_w, clamp it into [box_left, box_left+box_w]
    // and either marquee-scroll it (focused/center row) or ellipsize it (other rows). Labels
    // that already fit keep their original centered auto-width layout. Generic so any long value
    // (MAC / Commit / IP / hostname / version / long option) is handled, not just one screen.
    static void apply_overflow_handling(lv_obj_t *lbl, int box_left, int box_w, bool focused);
    static void apply_fixed_label_box(lv_obj_t *lbl, int x, int y, int w, bool scroll);
    static void clamp_label_box(lv_obj_t *lbl, int x, int w, bool scroll);

    // Same as apply_overflow_handling but keeps the clamped box centered on
    // center_x, so an overflowing (marquee) value stays visually centered
    // instead of drifting to a fixed left edge.
    static void apply_overflow_centered(lv_obj_t *lbl, int center_x, int box_w, bool focused);

    // ==================== Main carousel view ====================
    void build_main_view();

    // Animate scroll: direction = -1 (up) or +1 (down)
    void animate_scroll(int direction);
    void cancel_scroll_animations();
    static void anim_done_cb(lv_anim_t *a) noexcept;

    // ==================== Sub view ====================
    void build_sub_view();

    // Position the far-right hint (ok:xxx). If it is wider than RIGHT_HINT_BOX_W it is
    // clamped into a right-edge box and marquee-scrolled (so long hints like "ok:disable"
    // don't overlap the toggle indicator); otherwise it keeps its natural right-aligned pos.
    void apply_right_hint_overflow(lv_obj_t *hint, int row_top_y);

    // ==================== Value select view (3rd level) ====================
    void build_value_view();

    void rebuild_view();

    // Bounce animation for orange arrows (small Y displacement + return)
    void bounce_arrow(lv_obj_t *arrow, int direction);

    // Dual-container slide transition
    // direction: +1 = enter deeper (old slides left, new slides in from right)
    //            -1 = go back (old slides right, new slides in from left)
    void slide_transition(int direction);
    static void slide_old_done_cb(lv_anim_t *a) noexcept;

    // Enter deeper level (old slides left, new from right)
    void transition_enter_level();

    // Return to shallower level (old slides right, new from left)
    void transition_back_level();

    // ==================== Events ====================
    void event_handler_init();
    static void static_handler(lv_event_t *e) noexcept;

    uint32_t last_repeat_tick_ = 0;
    static constexpr uint32_t REPEAT_INTERVAL_MS = 300;

    void on_event(lv_event_t *e);
    void on_root_deleted();
    static uint32_t remap_fzxc(uint32_t key);
    void handle_main_key(uint32_t key);
    void handle_sub_key(uint32_t key);
    void handle_value_key(uint32_t key);


};
