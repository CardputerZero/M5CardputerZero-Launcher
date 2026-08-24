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
#include "setting_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_componens.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

extern "C" {
extern const lv_image_dsc_t setting_red_up;
extern const lv_image_dsc_t setting_red_down;
extern const lv_image_dsc_t setting_right_arrow;
extern const lv_image_dsc_t setting_ok;
extern const lv_image_dsc_t setting_cross;
}

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

    void AnimateNextIn(std::function<void()> animate_over_func) override
    {
        if (roller3_ && !page3_transitioning_) {
            page3_animation_over_ = std::move(animate_over_func);
            start_page3_transition(false);
        } else if (animate_over_func) {
            animate_over_func();
        }
    }

    void AnimateNextOut(std::function<void()> animate_over_func) override
    {
        if (roller3_ && !page3_transitioning_) {
            page3_animation_over_ = std::move(animate_over_func);
            start_page3_transition(true);
        } else if (animate_over_func) {
            animate_over_func();
        }
    }

    void LoadNextPage() override
    {
        if (item_count_ == 0 || selected_index < 0 || selected_index >= static_cast<int32_t>(item_count_)) {
            return;
        }

        auto selected_node = std::next(parent_node_.begin(), selected_index);
        if (!selected_node->page_factory || page3_transitioning_ || roller3_ || !parent_) return;

        lv_group_t *group = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr;

        roller3_ = selected_node->page_factory(ui_APP_Container, selected_node,
                                               std::bind(&LvSettingRollerPage2::LeaveNextPage, this));
        if (!roller3_ || !roller3_->Get()) {
            roller3_.reset();
            return;
        }

        if (ComponensObj && group) lv_group_remove_obj(ComponensObj);
        input_group_ = group;
        SetSelfUiMode(selected_node->page_type);

        lv_obj_set_style_translate_x(roller3_->Get(), 0, LV_PART_MAIN);
        lv_obj_set_x(roller3_->Get(), metric(LayoutMetric::PageWidth));
        AnimateNextOut(nullptr);
    }

    void LeaveNextPage() override
    {
        back_from_third_page();
    }

    LvSettingRollerPage2() = default;
    ~LvSettingRollerPage2()
    {
        cancel_async_tasks();
        if (roller3_ && roller3_->Get()) {
            lv_anim_del(roller3_->Get(), nullptr);
            if (input_group_) lv_group_remove_obj(roller3_->Get());
        }
        roller3_.reset();
        stop_page3_animations();
        if (ComponensObj) {
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
        if (utility_obj_) {
            lv_obj_delete(utility_obj_);
            utility_obj_ = nullptr;
        }
        selection_bg_ = nullptr;
        right_arrow_  = nullptr;
        arrow_up_     = nullptr;
        arrow_down_   = nullptr;
        hint_         = nullptr;
    }

    LvSettingRollerPage2(lv_obj_t *parent, const NodeIter &parent_node)
        : parent_(parent), ui_APP_Container(parent), parent_node_(parent_node)
    {
        create_ui(parent);
    }

    LvSettingRollerPage2(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback)
        : parent_(parent), ui_APP_Container(parent), parent_node_(parent_node)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
    }

    static void style_label(lv_obj_t *label, int distance, bool compact = false)
    {
        if (!label) return;

        int font_size  = 10;
        int opa        = 130;
        uint32_t color = 0x555555;
        if (distance == 0) {
            font_size = 16;
            opa       = 255;
            color     = 0xFFFFFF;
        } else if (distance == 1) {
            font_size = 12;
            opa       = 220;
            color     = 0xAAAAAA;
        } else if (distance == 2) {
            font_size = 12;
            opa       = 170;
            color     = 0x777777;
        }

        lv_obj_set_style_text_font(
            label, cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_opa(label, opa, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(label);

        const bool focused      = distance == 0;
        const int natural_width = lv_obj_get_width(label);
        if (compact) {
            lv_obj_set_width(label, metric(LayoutMetric::LabelBoxW));
            lv_label_set_long_mode(label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, metric(LayoutMetric::LabelBoxX));
        } else if (natural_width > metric(LayoutMetric::LabelBoxW)) {
            lv_obj_set_width(label, metric(LayoutMetric::LabelBoxW));
            lv_label_set_long_mode(label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, metric(LayoutMetric::LabelBoxX));
        } else {
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_update_layout(label);
            const int label_x = metric(LayoutMetric::LabelCenterX) - lv_obj_get_width(label) / 2;
            lv_obj_set_x(label, label_x < metric(LayoutMetric::LabelBoxX) ? metric(LayoutMetric::LabelBoxX) : label_x);
        }

        const int label_y = (metric(LayoutMetric::RowH) - lv_obj_get_height(label)) / 2;
        lv_obj_set_y(label, label_y < 0 ? 0 : label_y);
    }

    void refresh_status_label_layout(lv_obj_t *label)
    {
        if (!label || !ComponensObj) return;

        lv_obj_t *row = lv_obj_get_parent(label);
        if (!row || lv_obj_get_parent(row) != ComponensObj) return;

        const int index = static_cast<int>(lv_obj_get_index(row));
        style_label(label, std::abs(index - selected_index), NextActive);
    }

    void SetSelfUiMode(PageType mode) override
    {
        if (mode == PageType::FullCustom) return;

        const bool enabled = mode == PageType::NextPageNeeded;
        NextActive = enabled;

        update_arrow_visibility();

        if (right_arrow_) {
            if (enabled)
                lv_obj_add_flag(right_arrow_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_remove_flag(right_arrow_, LV_OBJ_FLAG_HIDDEN);
        }

        if (hint_) {
            if (enabled)
                lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN);
        }

        if (!ComponensObj) return;
        const uint32_t child_count = lv_obj_get_child_count(ComponensObj);
        for (uint32_t index = 0; index < child_count; ++index) {
            lv_obj_t *row   = lv_obj_get_child(ComponensObj, index);
            lv_obj_t *label = row ? lv_obj_get_child(row, 0) : nullptr;
            if (!label) continue;

            const int distance = std::abs(static_cast<int32_t>(index) - selected_index);
            style_label(label, distance, enabled);
        }
    }

    void update_arrow_visibility()
    {
        const bool show_arrows = item_count_ > 1 && !NextActive;
        if (arrow_up_) {
            if (show_arrows)
                lv_obj_remove_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
        }
        if (arrow_down_) {
            if (show_arrows)
                lv_obj_remove_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    struct StatusQueryResult {
        bool success = false;
        bool enabled = false;
    };

    using AsyncTaskContext    = DComponens::LvglComponensBase::AsyncTaskContext;
    using StatusTaskCallbacks = DComponens::LvglComponensBase::AsyncTaskCallbacks<StatusQueryResult>;

    static void set_status_icon(lv_obj_t *icon_obj, bool enabled)
    {
        if (!icon_obj) return;

        lv_img_set_src(icon_obj, enabled ? &setting_ok : &setting_cross);
        lv_obj_update_layout(icon_obj);
        lv_obj_set_pos(icon_obj, metric(LayoutMetric::StatusIconX) + (enabled ? 0 : 1),
                       (metric(LayoutMetric::RowH) - lv_obj_get_height(icon_obj)) / 2);
    }

    static void set_status_error(lv_obj_t *icon_obj, lv_obj_t *status_label)
    {
        if (status_label) lv_label_set_text(status_label, "Err");
        set_status_icon(icon_obj, false);
    }

    void request_status_refresh(lv_obj_t *icon_obj, lv_obj_t *status_label, const NodeIter &selected_node)
    {
        if (!icon_obj || !status_label || !selected_node->Componens_api) return;

        auto dot_count         = std::make_shared<uint8_t>(0);
        auto last_label_update = std::make_shared<AsyncTaskContext::Clock::time_point>(AsyncTaskContext::Clock::now());
        auto operation_started = std::make_shared<std::atomic_bool>(false);

        StatusTaskCallbacks callbacks;
        callbacks.execute = [selected_node, operation_started]() -> StatusQueryResult {
            SettingApiReadFlagTimeStartData result = std::make_tuple(false, operation_started.get());
            selected_node->Componens_api(SettingApiReadFlagTimeStart, &result);
            return {true, std::get<0>(result)};
        };
        callbacks.on_start = [this, status_label](AsyncTaskContext &) {
            lv_label_set_text(status_label, "Sel.");
            refresh_status_label_layout(status_label);
        };
        callbacks.on_submitted = [this, status_label, last_label_update](AsyncTaskContext &) {
            *last_label_update = AsyncTaskContext::Clock::now();
            lv_label_set_text(status_label, "Chk.");
            refresh_status_label_layout(status_label);
        };
        callbacks.on_wait = [this, status_label, dot_count, last_label_update](AsyncTaskContext &) {
            const auto now = AsyncTaskContext::Clock::now();
            if (now - *last_label_update < std::chrono::seconds(1)) return;

            *last_label_update = now;
            *dot_count         = static_cast<uint8_t>((*dot_count + 1) % 2);
            lv_label_set_text(status_label, *dot_count ? "Wait" : "Chk.");
            refresh_status_label_layout(status_label);
        };
        callbacks.on_complete = [this, icon_obj, status_label, selected_node](AsyncTaskContext &,
                                                                                const StatusQueryResult &result) {
            if (!result.success) {
                std::printf("[LvSettingRollerPage2] status query failed\n");
                set_status_error(icon_obj, status_label);
                refresh_status_label_layout(status_label);
                return;
            }

            lv_label_set_text(status_label, selected_node->label.c_str());
            set_status_icon(icon_obj, result.enabled);
            refresh_status_label_layout(status_label);
        };
        callbacks.on_exception = [this, icon_obj, status_label](AsyncTaskContext &, std::exception_ptr) {
            std::printf("[LvSettingRollerPage2] status query raised an exception\n");
            set_status_error(icon_obj, status_label);
            refresh_status_label_layout(status_label);
        };
        callbacks.on_timeout = [this, icon_obj, status_label](AsyncTaskContext &) {
            std::printf("[LvSettingRollerPage2] status query timed out\n");
            set_status_error(icon_obj, status_label);
            refresh_status_label_layout(status_label);
        };
        callbacks.on_schedule_failed = [this, icon_obj, status_label](AsyncTaskContext &) {
            std::printf("[LvSettingRollerPage2] status query could not be scheduled\n");
            set_status_error(icon_obj, status_label);
            refresh_status_label_layout(status_label);
        };

        run_async_task(std::move(callbacks));
    }

    void back_from_third_page()
    {
        if (!roller3_ || page3_transitioning_) return;
        AnimateNextIn(nullptr);
    }

    void stop_page3_animations()
    {
        auto stop = [](lv_obj_t *object) {
            if (object) lv_anim_del(object, nullptr);
        };
        stop(selection_bg_);
        stop(ComponensObj);
        stop(arrow_up_);
        stop(arrow_down_);
        stop(right_arrow_);
        stop(hint_);
        if (roller3_ && roller3_->Get()) stop(roller3_->Get());
    }

    int page_object_base_x(lv_obj_t *object) const
    {
        if (object == selection_bg_ || object == ComponensObj) return metric(LayoutMetric::PanelX);
        if (object == arrow_up_) return arrow_up_base_x_;
        if (object == arrow_down_) return arrow_down_base_x_;
        if (object == right_arrow_) return right_arrow_base_x_;
        if (object == hint_) return hint_base_x_;
        return object ? lv_obj_get_x(object) : 0;
    }

    void animate_object_to(lv_obj_t *object, int end_x)
    {
        if (!object) return;

        lv_anim_del(object, nullptr);
        const int start_x = lv_obj_get_x(object);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, object);
        lv_anim_set_exec_cb(&animation, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_x));
        lv_anim_set_values(&animation, start_x, end_x);
        lv_anim_set_time(&animation, metric(LayoutMetric::Page3AnimMs));
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        if (!lv_anim_start(&animation)) lv_obj_set_x(object, end_x);
    }

    void animate_page3_root(bool entering)
    {
        if (!roller3_ || !roller3_->Get()) return;

        lv_obj_t *root = roller3_->Get();
        lv_anim_del(root, nullptr);
        const int start_x = entering ? metric(LayoutMetric::PageWidth) : metric(LayoutMetric::Page3X);
        const int end_x = entering ? metric(LayoutMetric::Page3X) : metric(LayoutMetric::PageWidth);
        lv_obj_set_x(root, start_x);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, root);
        lv_anim_set_exec_cb(&animation, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_x));
        lv_anim_set_values(&animation, start_x, end_x);
        lv_anim_set_time(&animation, metric(LayoutMetric::Page3AnimMs));
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_user_data(&animation, this);
        lv_anim_set_completed_cb(&animation, entering ? &LvSettingRollerPage2::page3_enter_done_cb
                                                      : &LvSettingRollerPage2::page3_leave_done_cb);
        if (!lv_anim_start(&animation)) {
            lv_obj_set_x(root, end_x);
            finish_page3_transition(entering);
        }
    }

    void animate_first_page_objects(bool entering)
    {
        if (!ui_APP_Container || !utility_obj_) return;

        const uint32_t child_count = lv_obj_get_child_count(ui_APP_Container);
        for (uint32_t index = 0; index < child_count; ++index) {
            lv_obj_t *object = lv_obj_get_child(ui_APP_Container, index);
            if (!object || object == utility_obj_) break;
            const int offset = entering ? -metric(LayoutMetric::PageWidth) : metric(LayoutMetric::PageWidth);
            animate_object_to(object, lv_obj_get_x(object) + offset);
        }
    }

    int page2_target_x(lv_obj_t *object, bool entering) const
    {
        const int base_x = page_object_base_x(object);
        if (object == selection_bg_ || object == ComponensObj) {
            return entering ? 0 : base_x;
        }
        return entering ? base_x - metric(LayoutMetric::PanelX) : base_x;
    }

    void start_page3_transition(bool entering)
    {
        if (!roller3_ || !roller3_->Get()) return;

        page3_transitioning_ = true;
        if (!entering && input_group_) {
            lv_group_remove_obj(roller3_->Get());
        }
        animate_first_page_objects(entering);
        animate_object_to(selection_bg_, page2_target_x(selection_bg_, entering));
        animate_object_to(ComponensObj, page2_target_x(ComponensObj, entering));
        animate_object_to(arrow_up_, page2_target_x(arrow_up_, entering));
        animate_object_to(arrow_down_, page2_target_x(arrow_down_, entering));
        animate_object_to(right_arrow_, page2_target_x(right_arrow_, entering));
        animate_object_to(hint_, page2_target_x(hint_, entering));
        animate_page3_root(entering);
    }

    static void page3_enter_done_cb(lv_anim_t *animation)
    {
        auto *self = static_cast<LvSettingRollerPage2 *>(lv_anim_get_user_data(animation));
        if (self) self->finish_page3_transition(true);
    }

    static void page3_leave_done_cb(lv_anim_t *animation)
    {
        auto *self = static_cast<LvSettingRollerPage2 *>(lv_anim_get_user_data(animation));
        if (self) self->finish_page3_transition(false);
    }

    void finish_page3_transition(bool entering)
    {
        if (entering) {
            if (roller3_ && roller3_->Get()) {
                lv_obj_set_x(roller3_->Get(), metric(LayoutMetric::Page3X));
                if (input_group_) {
                    lv_group_add_obj(input_group_, roller3_->Get());
                    lv_group_focus_obj(roller3_->Get());
                }
            }
            page3_transitioning_ = false;
            invoke_page3_animation_callback();
            return;
        }

        lv_group_t *group = input_group_;
        if (roller3_ && roller3_->Get()) {
            if (group) lv_group_remove_obj(roller3_->Get());
            roller3_.reset();
        }
        if (ComponensObj && group) {
            lv_group_add_obj(group, ComponensObj);
            lv_group_focus_obj(ComponensObj);
        }
        SetSelfUiMode(PageType::Normal);
        page3_transitioning_ = false;
        invoke_page3_animation_callback();
    }

    void invoke_page3_animation_callback()
    {
        auto animate_over_func = std::move(page3_animation_over_);
        page3_animation_over_  = nullptr;
        if (animate_over_func) animate_over_func();
    }

    void scroll_to_selected(lv_obj_t *cont, bool animated)
    {
        if (!cont || item_count_ == 0) return;

        lv_obj_t *item = lv_obj_get_child(cont, selected_index);
        if (!item) return;

        lv_obj_scroll_to_view(item, animated ? LV_ANIM_ON : LV_ANIM_OFF);
        lv_obj_send_event(cont, LV_EVENT_SCROLL, cont);
        update_arrow_visibility();
    }

    void select(int index)
    {
        const int count = static_cast<int>(item_count_);
        if (count <= 0) return;
        if (index < 0) index = 0;
        if (index >= count) index = count - 1;
        selected_index = index;
        if (ComponensObj) {
            scroll_to_selected(ComponensObj, false);
        }
    }

    static void scroll_event_cb(lv_event_t *event)
    {
        lv_obj_t *cont    = lv_event_get_target(event);
        void *event_param = lv_event_get_param(event);
        auto *page        = static_cast<LvSettingRollerPage2 *>(lv_event_get_user_data(event));
        if (!cont || !page) return;

        if (static_cast<void *>(page->ComponensObj) != event_param) {
            return;
        }

        const uint32_t child_count = lv_obj_get_child_count(cont);
        for (uint32_t i = 0; i < child_count; ++i) {
            lv_obj_t *row   = lv_obj_get_child(cont, i);
            lv_obj_t *label = lv_obj_get_child(row, 0);
            if (!label) continue;

            const int distance = std::abs(static_cast<int32_t>(i) - page->selected_index);
            style_label(label, distance, page->NextActive);
        }
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;
        parent_       = parent;

        utility_obj_ = lv_obj_create(parent);
        if (!utility_obj_) return;
        lv_obj_set_size(utility_obj_, metric(LayoutMetric::PageWidth), metric(LayoutMetric::PanelH));
        lv_obj_set_pos(utility_obj_, 0, 0);
        lv_obj_set_style_bg_opa(utility_obj_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(utility_obj_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(utility_obj_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(utility_obj_, 0, LV_PART_MAIN);
        lv_obj_clear_flag(utility_obj_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(utility_obj_, LV_OBJ_FLAG_SCROLLABLE);

        selection_bg_ = lv_obj_create(utility_obj_);
        if (!selection_bg_) return;
        lv_obj_set_size(selection_bg_, metric(LayoutMetric::PanelW), metric(LayoutMetric::BarH));
        lv_obj_set_pos(selection_bg_, metric(LayoutMetric::PanelX), metric(LayoutMetric::BarY));
        lv_obj_set_style_bg_color(selection_bg_, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(selection_bg_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(selection_bg_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(selection_bg_, 0, LV_PART_MAIN);
        lv_obj_clear_flag(selection_bg_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(selection_bg_, LV_OBJ_FLAG_SCROLLABLE);

        right_arrow_ = lv_img_create(utility_obj_);
        if (right_arrow_) {
            lv_img_set_src(right_arrow_, &setting_right_arrow);
            lv_image_set_pivot(right_arrow_, 0, 0);
            lv_image_set_scale(right_arrow_, 224);
            lv_obj_update_layout(right_arrow_);
            lv_obj_set_pos(
                right_arrow_, metric(LayoutMetric::PanelX) - lv_obj_get_width(right_arrow_) - 4,
                metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(right_arrow_)) / 2);
            lv_obj_update_layout(utility_obj_);
            right_arrow_base_x_ = lv_obj_get_x(right_arrow_);
        }

        ComponensObj = lv_obj_create(utility_obj_);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj, metric(LayoutMetric::PanelW), metric(LayoutMetric::PanelH));
        lv_obj_set_pos(ComponensObj, metric(LayoutMetric::PanelX), 0);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_top(ComponensObj, metric(LayoutMetric::EdgePadding), LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(ComponensObj, metric(LayoutMetric::EdgePadding), LV_PART_MAIN);
        lv_obj_set_style_pad_row(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(ComponensObj, true, LV_PART_MAIN);
        lv_obj_set_flex_flow(ComponensObj, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(ComponensObj, LV_DIR_VER);
        lv_obj_set_scroll_snap_y(ComponensObj, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(ComponensObj, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
        lv_obj_add_event_cb(ComponensObj, scroll_event_cb, LV_EVENT_SCROLL, this);
        DComponens::lvgl_bind_event(ComponensObj, LV_EVENT_KEY, NULL,
                                    std::bind(&LvSettingRollerPage2::handle_key_event, this, std::placeholders::_1));

        for (auto it = parent_node_.begin(); it != parent_node_.end(); ++it) {
            lv_obj_t *row = lv_obj_create(ComponensObj);
            if (!row) continue;
            lv_obj_set_size(row, metric(LayoutMetric::PanelW), metric(LayoutMetric::RowH));
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *label = lv_label_create(row);
            if (label) lv_label_set_text(label, it->label.c_str());

            if (it->icon_enabled) {
                lv_obj_t *status_icon = lv_img_create(row);
                request_status_refresh(status_icon, label, it);
            }
        }
        item_count_ = lv_obj_get_child_count(ComponensObj);

        arrow_up_ = lv_img_create(utility_obj_);
        if (arrow_up_) {
            lv_img_set_src(arrow_up_, &setting_red_up);
            lv_obj_update_layout(arrow_up_);
            lv_obj_set_pos(
                arrow_up_,
                metric(LayoutMetric::PanelX) + metric(LayoutMetric::LabelCenterX) - lv_obj_get_width(arrow_up_) / 2, 2);
            lv_obj_update_layout(utility_obj_);
            arrow_up_base_x_ = lv_obj_get_x(arrow_up_);
        }

        arrow_down_ = lv_img_create(utility_obj_);
        if (arrow_down_) {
            lv_img_set_src(arrow_down_, &setting_red_down);
            lv_obj_update_layout(arrow_down_);
            lv_obj_set_pos(
                arrow_down_,
                metric(LayoutMetric::PanelX) + metric(LayoutMetric::LabelCenterX) - lv_obj_get_width(arrow_down_) / 2,
                metric(LayoutMetric::PanelH) - lv_obj_get_height(arrow_down_) - 4);
            lv_obj_update_layout(utility_obj_);
            arrow_down_base_x_ = lv_obj_get_x(arrow_down_);
        }

        hint_ = lv_label_create(utility_obj_);
        if (hint_) {
            lv_label_set_text(hint_, "ok:enter");
            lv_obj_set_style_text_color(hint_, lv_color_hex(0x00CC66), LV_PART_MAIN);
            lv_obj_set_style_text_font(hint_, cp0_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
                                       LV_PART_MAIN);
            lv_obj_update_layout(hint_);
            lv_obj_set_pos(hint_,
                           metric(LayoutMetric::PanelX) + metric(LayoutMetric::PanelW) - 6 - lv_obj_get_width(hint_),
                           metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(hint_)) / 2);
            lv_obj_update_layout(utility_obj_);
            hint_base_x_ = lv_obj_get_x(hint_);
        }

        if (item_count_ > 0) {
            lv_obj_update_layout(ComponensObj);
            scroll_to_selected(ComponensObj, false);
        }
        update_arrow_visibility();
    }
    void handle_key_event(lv_event_t *event)
    {
        lv_obj_t *cont = lv_event_get_target(event);
        if (!cont || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (LeaveSelfPage) LeaveSelfPage();
            lv_event_stop_processing(event);
            return;
        }
        if (item_count_ == 0) {
            lv_event_stop_processing(event);
            return;
        }

        if (key == LV_KEY_UP) {
            const bool wrapped = selected_index == 0;
            selected_index     = wrapped ? static_cast<int32_t>(item_count_ - 1) : selected_index - 1;
            scroll_to_selected(cont, !wrapped);
        } else if (key == LV_KEY_DOWN) {
            const bool wrapped = selected_index == static_cast<int32_t>(item_count_ - 1);
            selected_index     = wrapped ? 0 : selected_index + 1;
            scroll_to_selected(cont, !wrapped);
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            // Find the setting node corresponding to the currently selected item.
            auto selected_node = std::next(parent_node_.begin(), selected_index);

            // Open the next page when the selected node provides a page factory.
            if (selected_node->page_factory) {
                LoadNextPage();
                // Activate the selected component through its settings API.
            } else if (selected_node->Componens_api) {
                selected_node->Componens_api(SettingApiActivate, this);
                // Refresh the status icon and label when the node displays an icon.
                if (selected_node->icon_enabled) {
                    request_status_refresh(get_selection_icon(selected_index), get_selection_lable(selected_index),
                                           selected_node);
                }
                // Fall back to the page-selection callback when no other handler is available.
            } else if (on_selected_page) {
                on_selected_page(selected_index);
            }
        }

        lv_event_stop_processing(event);
    }

private:
    lv_obj_t *get_selection_lable(int index)
    {
        return lv_obj_get_child(lv_obj_get_child(ComponensObj, index), 0);
    }
    lv_obj_t *get_selection_icon(int index)
    {
        return lv_obj_get_child(lv_obj_get_child(ComponensObj, index), 1);
    }

private:
    lv_obj_t *parent_          = nullptr;
    lv_obj_t *ui_APP_Container = nullptr;
    NodeIter parent_node_;
    uint32_t item_count_ = 0;
    std::unique_ptr<DComponens::LvglComponensBase> roller3_;
    bool page3_transitioning_                   = false;
    std::function<void()> page3_animation_over_ = nullptr;
    lv_group_t *input_group_                    = nullptr;
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
};
