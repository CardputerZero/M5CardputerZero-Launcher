/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include "../launcher_ui_app_page.hpp"
#include "../model/ip_panel_model.hpp"
#include "../model/page_timer_lifecycle.hpp"

// ============================================================
//  IP panel screen  UIIpPanelPage
//  Screen resolution: 320 x 170  (top bar20px, ui_APP_Container 320x150)
//
//  View state:
//    VIEW_MAIN    — list (auto-refresh every second and show interface information)
// ============================================================

class UIIpPanelPage : public AppPage
{
    enum class ViewState { MAIN, HELP };

public:
    UIIpPanelPage();
    ~UIIpPanelPage() override;

private:
    // ==================== Data members ====================
    IpPanelModel model_;
    lv_obj_t *background_ = nullptr;
    lv_obj_t *list_container_ = nullptr;
    lv_obj_t *help_container_ = nullptr;
    ViewState view_state_ = ViewState::MAIN;

    // Layout constants (content area 320x150, title bar 22px, remaining 128px)
    static constexpr int ITEM_H       = 32;   // row height
    static constexpr int VISIBLE_ROWS = 4;    // visible row count (128/32 = 4)
    static constexpr int TITLE_H      = 22;   // title bar height
    static constexpr int LIST_H       = 128;  // list area height

    // timer handle
    PageTimerLifecycle<lv_timer_t *> refresh_timer_;
    bool refresh_enabled_ = true;

    // ==================== Read system network interface information ====================
    bool fetch_iface_list();

    // ==================== UI construction ====================
    void create_ui();
    bool build_help_view();
    void show_help();
    void close_help();

    // ==================== Build list rows ====================
    void build_list_rows();

    // ==================== Create one row ====================
    void create_row(lv_obj_t *parent, int visual_row, size_t index, bool selected);

    // ==================== Periodic refresh callback ====================
    static void static_timer_cb(lv_timer_t *timer) noexcept;
    void on_timer_refresh();

    // ==================== Event binding ====================
    void event_handler_init();
    static void static_lvgl_handler(lv_event_t *e) noexcept;
    static void static_owned_obj_delete_cb(lv_event_t *event) noexcept;
    static uint32_t fzxc_to_lv_arrow(uint32_t key);

    void event_handler(lv_event_t *e);
    void detach_delete_callbacks();
};
