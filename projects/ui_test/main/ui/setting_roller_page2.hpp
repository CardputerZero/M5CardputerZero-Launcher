/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdio>
#include <functional>
#include <iterator>
#include <chrono>
#include <future>
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
    static constexpr int PANEL_X        = 120;
    static constexpr int PANEL_W        = 200;
    static constexpr int PANEL_H        = 150;
    static constexpr int ROW_H          = 21;
    static constexpr int CENTER_ROW     = 3;
    static constexpr int EDGE_PADDING   = ROW_H * CENTER_ROW;
    static constexpr int BAR_H          = 21;
    static constexpr int BAR_Y          = 66;
    static constexpr int LABEL_CENTER_X = 40;
    static constexpr int LABEL_BOX_X    = 0;
    static constexpr int LABEL_BOX_W    = 80;
    static constexpr int STATUS_ICON_X  = 100;
    static constexpr int PAGE_WIDTH     = 320;
    static constexpr int PAGE3_ANIM_MS  = 200;

    int32_t selected_index                    = 0;
    std::function<void(int)> on_selected_page = nullptr;
    std::function<void()> on_back             = nullptr;

    LvSettingRollerPage2() = default;
    ~LvSettingRollerPage2()
    {
        lifetime_token_.reset();
        if (roller3_ && roller3_->Get()) {
            lv_anim_del(roller3_->Get(), nullptr);
            if (input_group_)
                lv_group_remove_obj(roller3_->Get());
        }
        roller3_.reset();
        stop_page3_animations();
        if (ComponensObj) {
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
        if (selection_bg_) {
            lv_obj_delete(selection_bg_);
            selection_bg_ = nullptr;
        }
        if (arrow_up_) {
            lv_obj_delete(arrow_up_);
            arrow_up_ = nullptr;
        }
        if (arrow_down_) {
            lv_obj_delete(arrow_down_);
            arrow_down_ = nullptr;
        }
        if (hint_) {
            lv_obj_delete(hint_);
            hint_ = nullptr;
        }
    }

    LvSettingRollerPage2(lv_obj_t *parent, const NodeIter &parent_node)
        : parent_(parent), parent_node_(parent_node)
    {
        create_ui(parent);
    }

    LvSettingRollerPage2(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback)
        : parent_(parent), parent_node_(parent_node), on_back(std::move(back_callback))
    {
        create_ui(parent);
    }

    static void style_label(lv_obj_t *label, int distance)
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
        if (natural_width > LABEL_BOX_W) {
            lv_obj_set_width(label, LABEL_BOX_W);
            lv_label_set_long_mode(label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, LABEL_BOX_X);
        } else {
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_update_layout(label);
            const int label_x = LABEL_CENTER_X - lv_obj_get_width(label) / 2;
            lv_obj_set_x(label, label_x < LABEL_BOX_X ? LABEL_BOX_X : label_x);
        }

        const int label_y = (ROW_H - lv_obj_get_height(label)) / 2;
        lv_obj_set_y(label, label_y < 0 ? 0 : label_y);
    }

    void update_arrow_visibility()
    {
        const bool show_arrows = item_count_ > 1;
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

    struct StatusContext {
        struct QueryResult {
            bool success = false;
            bool enabled = false;
        };

        lv_obj_t *icon_obj     = nullptr;
        lv_obj_t *status_label = nullptr;
        NodeIter selected_node;
        std::chrono::system_clock::time_point timestamp;
        std::chrono::system_clock::time_point last_label_update;
        uint8_t dot_count = 0;
        bool waiting = false;
        std::shared_ptr<std::atomic_bool> timestarted = std::make_shared<std::atomic_bool>(true);
        std::shared_future<QueryResult> result;
        std::weak_ptr<bool> lifetime_token;

        StatusContext(lv_obj_t *icon, lv_obj_t *label, const NodeIter &node, std::weak_ptr<bool> token)
            : icon_obj(icon),
              status_label(label),
              selected_node(node),
              timestamp(std::chrono::system_clock::now()),
              last_label_update(timestamp),
              lifetime_token(std::move(token))
        {
        }
    };

    static void async_update_status_icon(void *user_data)
    {
        auto *context = static_cast<StatusContext *>(user_data);
        if (!context) return;

        if (context->lifetime_token.expired()) {
            delete context;
            return;
        }

        if (!context->icon_obj || !context->selected_node->Componens_api) {
            delete context;
            return;
        }

        if (!context->waiting) {
            const auto selected_node  = context->selected_node;
            const auto lifetime_token = context->lifetime_token;
            const auto timestarted = context->timestarted;

            context->timestarted->store(true, std::memory_order_release);

            context->result = std::async(
                                  std::launch::async,
                                  [selected_node, lifetime_token, timestarted]() -> StatusContext::QueryResult {
                                      if (lifetime_token.expired()) return {};
                                      try {
                                          SettingApiReadFlagTimeStartData result =
                                              std::make_tuple(false, timestarted.get());
                                          selected_node->Componens_api(SettingApiReadFlagTimeStart, &result);
                                          return {true, std::get<0>(result)};
                                      } catch (...) {
                                          return {};
                                      }
                                  })
                                  .share();
            context->waiting = true;
            context->timestamp         = std::chrono::system_clock::now();
            context->last_label_update = context->timestamp;
            context->dot_count         = 0;
            lv_label_set_text(context->status_label, "Chk.");
            if (lv_async_call(async_update_status_icon, context) != LV_RESULT_OK) {
                delete context;
            }
            return;
        }

        const auto now = std::chrono::system_clock::now();
        if (!context->timestarted->load(std::memory_order_acquire)) {
            context->timestamp = now;
            if (now - context->last_label_update >= std::chrono::seconds(1)) {
                context->dot_count = static_cast<uint8_t>((context->dot_count + 1) % 2);
                lv_label_set_text(context->status_label, context->dot_count ? "Wait" : "Chk.");
                context->last_label_update = now;
            }
        } else {
            if (now - context->timestamp >= std::chrono::seconds(3)) {
                std::printf("[LvSettingRollerPage2] status icon query failed: timeout\n");
                lv_label_set_text(context->status_label, "Err");
                lv_img_set_src(context->icon_obj, &setting_cross);
                delete context;
                return;
            }
        }

        if (context->result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            if (lv_async_call(async_update_status_icon, context) != LV_RESULT_OK) {
                delete context;
            }
            return;
        }

        const auto query_result = context->result.get();
        if (!query_result.success) {
            std::printf("[LvSettingRollerPage2] status icon query failed\n");
            lv_label_set_text(context->status_label, "Err");
            lv_img_set_src(context->icon_obj, &setting_cross);
            delete context;
            return;
        }

        lv_label_set_text(context->status_label, context->selected_node->label.c_str());
        lv_img_set_src(context->icon_obj, query_result.enabled ? &setting_ok : &setting_cross);
        lv_obj_update_layout(context->icon_obj);
        lv_obj_set_pos(context->icon_obj, STATUS_ICON_X + (query_result.enabled ? 0 : 1),
                       (ROW_H - lv_obj_get_height(context->icon_obj)) / 2);
        delete context;
    }

    void lvgl_async_update_status_icon(lv_obj_t *icon_obj, lv_obj_t *status_label, const NodeIter &selected_node)
    {
        if (!icon_obj || !status_label || !selected_node->Componens_api) return;

        lv_label_set_text(status_label, "Sel.");
        auto *context = new StatusContext(icon_obj, status_label, selected_node, lifetime_token_);
        if (lv_async_call(LvSettingRollerPage2::async_update_status_icon, context) != LV_RESULT_OK) {
            delete context;
        }
    }

    void update_status_icon(lv_obj_t *icon_obj, const NodeIter &selected_node)
    {
        if (!icon_obj || !selected_node->Componens_api) return;

        bool enabled = false;
        selected_node->Componens_api(SettingApiReadFlag, &enabled);
        lv_img_set_src(icon_obj, enabled ? &setting_ok : &setting_cross);
        lv_obj_update_layout(icon_obj);
        lv_obj_set_pos(icon_obj, STATUS_ICON_X + (enabled ? 0 : 1), (ROW_H - lv_obj_get_height(icon_obj)) / 2);
    }

    void key_event_cb(lv_event_t *event)
    {
        lv_obj_t *cont = lv_event_get_target(event);
        if (!cont || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (on_back) on_back();
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
            auto selected_node = std::next(parent_node_.begin(), selected_index);
            if (selected_node->Componens_api) {
                selected_node->Componens_api(SettingApiActivate, this);

                if (selected_node->icon_enabled) {
                    update_status_icon(lv_obj_get_child(lv_obj_get_child(cont, selected_index), 1), selected_node);
                }
            } else if (selected_node->page_factory) {
                create_third_page(selected_node);
            } else if (on_selected_page) {
                on_selected_page(selected_index);
            }
        }

        lv_event_stop_processing(event);
    }

    void create_third_page(const NodeIter &page_node)
    {
        if (page3_transitioning_ || roller3_ || !page_node->page_factory || !parent_)
            return;

        lv_group_t *group = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr;
        if (ComponensObj && group) lv_group_remove_obj(ComponensObj);
        input_group_ = group;

        roller3_ = page_node->page_factory(
            parent_,
            page_node,
            std::bind(&LvSettingRollerPage2::back_from_third_page, this));
        if (!roller3_ || !roller3_->Get()) {
            roller3_.reset();
            if (ComponensObj && group) {
                lv_group_add_obj(group, ComponensObj);
                lv_group_focus_obj(ComponensObj);
            }
            return;
        }

        lv_obj_set_x(roller3_->Get(), PAGE_WIDTH);
        if (group) {
            lv_group_add_obj(group, roller3_->Get());
            lv_group_focus_obj(roller3_->Get());
        }
        start_page3_transition(true);
    }

    void back_from_third_page()
    {
        if (!roller3_ || (page3_transitioning_ && !page3_entering_)) return;
        start_page3_transition(false);
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
        stop(hint_);
    }

    int page_object_base_x(lv_obj_t *object) const
    {
        if (object == selection_bg_ || object == ComponensObj) return PANEL_X;
        if (object == arrow_up_) return arrow_up_base_x_;
        if (object == arrow_down_) return arrow_down_base_x_;
        if (object == hint_) return hint_base_x_;
        return object ? lv_obj_get_x(object) : 0;
    }

    void animate_page_object(lv_obj_t *object, bool entering)
    {
        if (!object) return;

        lv_anim_del(object, nullptr);
        const int start_x = lv_obj_get_x(object);
        const int end_x   = page_object_base_x(object) + (entering ? -PAGE_WIDTH : 0);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, object);
        lv_anim_set_exec_cb(&animation, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_x));
        lv_anim_set_values(&animation, start_x, end_x);
        lv_anim_set_time(&animation, PAGE3_ANIM_MS);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        if (!lv_anim_start(&animation)) lv_obj_set_x(object, end_x);
    }

    void animate_page3_root(bool entering)
    {
        if (!roller3_ || !roller3_->Get()) return;

        lv_obj_t *root = roller3_->Get();
        lv_anim_del(root, nullptr);
        const int start_x = lv_obj_get_x(root);
        const int end_x   = entering ? 0 : PAGE_WIDTH;
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, root);
        lv_anim_set_exec_cb(&animation, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_x));
        lv_anim_set_values(&animation, start_x, end_x);
        lv_anim_set_time(&animation, PAGE3_ANIM_MS);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_user_data(&animation, this);
        lv_anim_set_completed_cb(&animation,
                                 entering ? &LvSettingRollerPage2::page3_enter_done_cb
                                          : &LvSettingRollerPage2::page3_leave_done_cb);
        if (!lv_anim_start(&animation)) {
            lv_obj_set_x(root, end_x);
            finish_page3_transition(entering);
        }
    }

    void start_page3_transition(bool entering)
    {
        if (!roller3_ || !roller3_->Get()) return;

        page3_transitioning_ = true;
        page3_entering_      = entering;
        animate_page_object(selection_bg_, entering);
        animate_page_object(ComponensObj, entering);
        animate_page_object(arrow_up_, entering);
        animate_page_object(arrow_down_, entering);
        animate_page_object(hint_, entering);
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
                lv_obj_set_x(roller3_->Get(), 0);
                if (input_group_) lv_group_focus_obj(roller3_->Get());
            }
            page3_transitioning_ = false;
            page3_entering_      = false;
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
        page3_transitioning_ = false;
        page3_entering_      = false;
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
            style_label(label, distance);
        }
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;
        parent_ = parent;

        selection_bg_ = lv_obj_create(parent);
        if (!selection_bg_) return;
        lv_obj_set_size(selection_bg_, PANEL_W, BAR_H);
        lv_obj_set_pos(selection_bg_, PANEL_X, BAR_Y);
        lv_obj_set_style_bg_color(selection_bg_, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(selection_bg_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(selection_bg_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(selection_bg_, 0, LV_PART_MAIN);
        lv_obj_clear_flag(selection_bg_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(selection_bg_, LV_OBJ_FLAG_SCROLLABLE);

        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj, PANEL_W, PANEL_H);
        lv_obj_set_pos(ComponensObj, PANEL_X, 0);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_top(ComponensObj, EDGE_PADDING, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(ComponensObj, EDGE_PADDING, LV_PART_MAIN);
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
                                    std::bind(&LvSettingRollerPage2::key_event_cb, this, std::placeholders::_1));

        for (auto it = parent_node_.begin(); it != parent_node_.end(); ++it) {
            lv_obj_t *row = lv_obj_create(ComponensObj);
            if (!row) continue;
            lv_obj_set_size(row, PANEL_W, ROW_H);
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
                update_status_icon(status_icon, it);
            }
        }
        item_count_ = lv_obj_get_child_count(ComponensObj);

        arrow_up_ = lv_img_create(parent);
        if (arrow_up_) {
            lv_img_set_src(arrow_up_, &setting_red_up);
            lv_obj_update_layout(arrow_up_);
            lv_obj_set_pos(arrow_up_, PANEL_X + LABEL_CENTER_X - lv_obj_get_width(arrow_up_) / 2, 2);
            arrow_up_base_x_ = lv_obj_get_x(arrow_up_);
        }

        arrow_down_ = lv_img_create(parent);
        if (arrow_down_) {
            lv_img_set_src(arrow_down_, &setting_red_down);
            lv_obj_update_layout(arrow_down_);
            lv_obj_set_pos(arrow_down_, PANEL_X + LABEL_CENTER_X - lv_obj_get_width(arrow_down_) / 2,
                           PANEL_H - lv_obj_get_height(arrow_down_) - 4);
            arrow_down_base_x_ = lv_obj_get_x(arrow_down_);
        }

        hint_ = lv_label_create(parent);
        if (hint_) {
            lv_label_set_text(hint_, "ok:enter");
            lv_obj_set_style_text_color(hint_, lv_color_hex(0x00CC66), LV_PART_MAIN);
            lv_obj_set_style_text_font(hint_, cp0_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
                                       LV_PART_MAIN);
            lv_obj_update_layout(hint_);
            lv_obj_set_pos(hint_, PANEL_X + PANEL_W - 6 - lv_obj_get_width(hint_),
                           BAR_Y + (BAR_H - lv_obj_get_height(hint_)) / 2);
            hint_base_x_ = lv_obj_get_x(hint_);
        }

        if (item_count_ > 0) {
            scroll_to_selected(ComponensObj, false);
        }
        update_arrow_visibility();
    }

private:
    lv_obj_t *parent_ = nullptr;
    NodeIter parent_node_;
    std::shared_ptr<bool> lifetime_token_ = std::make_shared<bool>(true);
    uint32_t item_count_                  = 0;
    std::unique_ptr<DComponens::LvglComponensBase> roller3_;
    bool page3_transitioning_ = false;
    bool page3_entering_      = false;
    lv_group_t *input_group_ = nullptr;
    int arrow_up_base_x_ = 0;
    int arrow_down_base_x_ = 0;
    int hint_base_x_ = 0;
    lv_obj_t *selection_bg_               = nullptr;
    lv_obj_t *hint_                       = nullptr;
    lv_obj_t *arrow_up_                   = nullptr;
    lv_obj_t *arrow_down_                 = nullptr;
};
