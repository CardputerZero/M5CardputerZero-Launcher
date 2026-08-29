/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <functional>
#include <iterator>
#include <chrono>
#include <exception>
#include <memory>
#include <utility>
#include <tuple>

#include "cp0_font_service.hpp"
#include "cp0_lvgl_app_page_assets.h"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

class LvSettingRollerPage2 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        PanelX       = 120,
        PanelW       = 200,
        PanelH       = 150,
        RowH         = 21,
        CenterRow    = 3,
        EdgePadding  = RowH * CenterRow,
        BarH         = 21,
        BarY         = 66,
        LabelCenterX = 40,
        LabelBoxX    = 0,
        LabelBoxW    = 80,
        StatusIconX  = 100,
        Page3X       = 0,
        PageWidth    = 320,
        Page3AnimMs  = 200,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    int32_t selected_index                    = 0;
    std::function<void(int)> on_selected_page = nullptr;

    void AnimateNextIn(std::function<void()> animate_over_func) override;

    void AnimateNextOut(std::function<void()> animate_over_func) override;

    void LoadNextPage() override;

    void LeaveNextPage() override;

    LvSettingRollerPage2();
    ~LvSettingRollerPage2();

    LvSettingRollerPage2(lv_obj_t *parent, const NodeIter &parent_node);

    LvSettingRollerPage2(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback);

    static void style_label(lv_obj_t *label, int distance, bool compact = false);

    void set_status_hint(const char *text, uint32_t color);

    void SetSelfUiMode(PageType mode) override;

    std::string selected_entry_label() const;

    void show_power_warning();

    void set_compact_mode(bool enabled);

    void update_arrow_visibility();

private:
    struct StatusQueryResult {
        bool success = false;
        bool enabled = false;
    };

    using AsyncTaskContext    = DComponens::LvglComponensBase::AsyncTaskContext;
    using StatusTaskCallbacks = DComponens::LvglComponensBase::AsyncTaskCallbacks<StatusQueryResult>;

    static void set_status_icon(lv_obj_t *icon_obj, bool enabled);

    static void set_status_error(lv_obj_t *icon_obj);

    static void direct_status_timer_cb(lv_timer_t *timer);

    void stop_direct_status_poll();

    void request_status_refresh(lv_obj_t *icon_obj, const NodeIter &selected_node);

    void handle_power_warning_key(lv_event_t *event);

    void close_power_warning();

    void create_third_page(const NodeIter &page_node);

    void back_from_third_page();

    void stop_page3_animations();

    int page_object_base_x(lv_obj_t *object) const;

    void animate_object_to(lv_obj_t *object, int end_x);

    void animate_page3_root(bool entering);

    void animate_first_page_objects(bool entering);

    int page2_target_x(lv_obj_t *object, bool entering) const;

    void start_page3_transition(bool entering);

    static void page3_enter_done_cb(lv_anim_t *animation);

    static void page3_leave_done_cb(lv_anim_t *animation);

    void finish_page3_transition(bool entering);

    void invoke_page3_animation_callback();

    void scroll_to_selected(lv_obj_t *cont, bool animated);

    void select(int index);

    static void scroll_event_cb(lv_event_t *event);

    void create_ui(lv_obj_t *parent) override;

    void handle_key_event(lv_event_t *event);

private:
    lv_obj_t *parent_          = nullptr;
    lv_obj_t *ui_APP_Container = nullptr;
    lv_timer_t *direct_status_timer_ = nullptr;
    std::function<bool()> direct_status_poll_;
    NodeIter parent_node_;
    uint32_t item_count_ = 0;
    std::unique_ptr<DComponens::LvglComponensBase> roller3_;
    bool page3_transitioning_                   = false;
    std::function<void()> page3_animation_over_ = nullptr;
    lv_group_t *input_group_                    = nullptr;
    bool compact_mode_                          = false;
    int arrow_up_base_x_                        = 0;
    int arrow_down_base_x_                      = 0;
    int right_arrow_base_x_                     = 0;
    int hint_base_x_                            = 0;
    lv_obj_t *selection_bg_                     = nullptr;
    lv_obj_t *right_arrow_                      = nullptr;
    lv_obj_t *hint_                             = nullptr;
    lv_obj_t *arrow_up_                         = nullptr;
    lv_obj_t *arrow_down_                       = nullptr;
    lv_obj_t *utility_obj_                      = nullptr;
    lv_obj_t *power_warning_              = nullptr;
};
