/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdio>
#include <iterator>
#include <list>
#include <memory>

#include "cp0_font_service.hpp"
#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_componens.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY
#include "setting_roller_page2.hpp"
#include "setting_tree_types.hpp"

extern "C" {
extern const lv_image_dsc_t setting_red_up;
extern const lv_image_dsc_t setting_red_down;
extern const lv_image_dsc_t setting_right_arrow;
}

/**
 * Top-level scrolling selector for the settings page.
 *
 * This component displays first-level entries under the Setting root node and provides:
 *
 * 1. 1. Move between entries with the Up and Down keys;
 * 2. 2. Always scroll the current entry into the centered highlight area;
 * 3. 3. Dynamically adjust font, color, and opacity based on the entry's distance from the center;
 * 4. 4. Use marquee scrolling for long selected text so it does not exceed the left entry area;
 * 5. 5. Open the corresponding LvSettingRollerPage2 after pressing Enter;
 * 6. 6. Switch keyboard focus and visible controls between the second-level and first-level pages.
 *
 * Each row in the top-level roller consists of an entry container and a Label. The entry container
 * is arranged by ComponensObj's vertical Flex layout, while this class manually controls the Label's horizontal position
 * to match the settings-page alignment used by APPLaunch.
 */
class LvSettingRoller : public DComponens::LvglComponensBase {
public:
    using Page3TransitionCallback = LvSettingRollerPage2::Page3TransitionCallback;

    // Fixed height of each row. Scroll distance, selection area, and text vertical centering are calculated from this value.
    static constexpr int ROW_H = 21;

    // Index of the center row in the visible area; three rows are reserved above and below.
    static constexpr int CENTER_ROW = 3;

    // Top and bottom padding, allowing the first and last entries to scroll to the center.
    static constexpr int EDGE_PADDING = ROW_H * CENTER_ROW;

    // Normal top-level entries are centered around this x-coordinate.
    static constexpr int LABEL_CENTER_X = 60;

    // Fixed starting x-coordinate of the left-side top-level entries after entering a second-level page.
    static constexpr int LABEL_BOX_X = 4;

    // Fixed display width of the left-side top-level entries after entering a second-level page.
    static constexpr int LABEL_BOX_W = 90;

    // Index of the currently selected top-level entry.
    int32_t selected_index                                  = 0;

    // Second-level roller object; created when entering a second-level page and destroyed when returning.
    std::unique_ptr<DComponens::LvglComponensBase> roller2_ = nullptr;

    void AnimateNextIn(std::function<void()> animate_over_func) override
    {
        animate_page3_objects(false, std::move(animate_over_func));
    }

    void AnimateNextOut(std::function<void()> animate_over_func) override
    {
        animate_page3_objects(true, std::move(animate_over_func));
    }

    void LoadNextPage() override
    {
        if (item_count_ == 0 || selected_index < 0 ||
            selected_index >= static_cast<int32_t>(item_count_)) {
            return;
        }

        auto selected_node = std::next(parent_node_.begin(), selected_index);
        if (!selected_node->page_factory) return;

        set_secondary_mode();
        input_group_ = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr;
        if (ComponensObj && input_group_) {
            lv_group_remove_obj(ComponensObj);
            top_in_group_ = false;
        }

        roller2_ = selected_node->page_factory(
            ui_APP_Container,
            selected_node,
            std::bind(&LvSettingRoller::LeaveNextPage, this));
        if (auto *page2 = dynamic_cast<LvSettingRollerPage2 *>(roller2_.get())) {
            page2->set_page3_transition_callback(
                std::bind(&LvSettingRoller::page3_transition_cb, this,
                          std::placeholders::_1, std::placeholders::_2));
        }
        if (input_group_ && roller2_ && roller2_->Get()) {
            lv_group_add_obj(input_group_, roller2_->Get());
            lv_group_focus_obj(roller2_->Get());
        }
        AnimateNextIn(nullptr);
    }

    void LeaveNextPage() override
    {
        lv_async_call(lvgl_back_this, this);
    }

    /**
     * Set the Label style based on the entry's distance from the selected center row.
     *
     * The meaning of distance is:
     * - 0：0: The selected row, using the largest font and highest brightness;
     * - 1：1: A row adjacent to the selected row, using a medium font and brightness;
     * - 2：2: Two rows away from the selected row, using a smaller font and brightness;
     * - Larger values: Use the default smaller font and lower brightness.
     *
     * When left_aligned is true, the top-level page has switched to the second-level page layout.
     * The top-level entries are no longer centered around LABEL_CENTER_X; they are fixed in the left-side area
     * from LABEL_BOX_X to LABEL_BOX_X + LABEL_BOX_W.
     */
    static void style_label(lv_obj_t *label, int distance, bool left_aligned = false)
    {
        if (!label) return;

        int font_size  = 12;
        int opa        = 130;
        uint32_t color = 0x555555;
        if (distance == 0) {
            font_size = 18;
            opa       = 255;
            color     = 0xFFFFFF;
        } else if (distance == 1) {
            font_size = 16;
            opa       = 220;
            color     = 0xAAAAAA;
        } else if (distance == 2) {
            font_size = 12;
            opa       = 170;
            color     = 0x777777;
        }

        // Set the current row font, color, and opacity first, then recalculate the text width using the final font.
        lv_obj_set_style_text_font(
            label, cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_opa(label, opa, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        // Restore the content width and disable scrolling first to obtain the text's natural width.
        // Re-enable marquee scrolling only when the entry is selected and the text actually exceeds the display box.
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(label);

        const bool focused = distance == 0;
        const int natural_width = lv_obj_get_width(label);
        if (left_aligned) {
            // Top-level entries on the left side of a second-level page use a fixed width and left alignment.
            lv_obj_set_width(label, LABEL_BOX_W);
            lv_label_set_long_mode(
                label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, LABEL_BOX_X);
        } else {
            // Normal entries that do not exceed the width are centered around the midpoint without crossing the minimum left margin.
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_update_layout(label);
            const int label_x = LABEL_CENTER_X - lv_obj_get_width(label) / 2;
            lv_obj_set_x(label, label_x < LABEL_BOX_X ? LABEL_BOX_X : label_x);
        }

        // Vertically center the text within the 21-pixel row; if the font is too tall, start at the top.
        const int label_y = (ROW_H - lv_obj_get_height(label)) / 2;
        lv_obj_set_y(label, label_y < 0 ? 0 : label_y);
    }

    /**
     * Handle keyboard events for the top-level roller.
     *
     * ESC/LEFT: Return to the parent page;
     * UP/DOWN: Cycle through entries and scroll the selected entry to the center;
     * ENTER: Invoke the current node callback or create the corresponding second-level page.
     */
    void handle_key_event(lv_event_t *e)
    {
        lv_obj_t *cont = lv_event_get_target(e);

        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (LeaveSelfPage) LeaveSelfPage();
            lv_event_stop_processing(e);
            return;
        }

        if (item_count_ == 0) {
            lv_event_stop_processing(e);
            return;
        }

        if (key == LV_KEY_UP) {
            // After reaching the first entry, continue from the last entry.
            const bool wrapped = selected_index == 0;
            selected_index = wrapped
                ? static_cast<int32_t>(item_count_ - 1)
                : selected_index - 1;
            scroll_to_selected(cont, !wrapped);
        } else if (key == LV_KEY_DOWN) {
            // After reaching the last entry, continue from the first entry.
            const bool wrapped = selected_index == static_cast<int32_t>(item_count_ - 1);
            selected_index = wrapped ? 0 : selected_index + 1;
            scroll_to_selected(cont, !wrapped);
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            // NodeIter and the roller child containers have the same order, so the index can locate the node.
            auto selected_node = std::next(parent_node_.begin(), selected_index);
            if (selected_node->page_factory) {
                LoadNextPage();
            }
        }
        lv_event_stop_processing(e);
    }

    /**
     * Scroll the entry at the specified index into the centered selection area.
     *
     * Use LVGL animation when animated is true; during initial creation, explicit selection, or cyclic
     * wraparound, positioning is normally done without animation to avoid unnatural long-distance scrolling between the ends.
     */
    void scroll_to_selected(lv_obj_t *cont, bool animated)
    {
        if (!cont || item_count_ == 0) return;

        lv_obj_t *item = lv_obj_get_child(cont, selected_index);
        if (!item) return;

        lv_obj_scroll_to_view(item, animated ? LV_ANIM_ON : LV_ANIM_OFF);
        lv_obj_send_event(cont, LV_EVENT_SCROLL, ComponensObj);
    }

    /**
     * Perform the actual return-to-top-level operation in the LVGL thread.
     *
     * The callback may originate from a second-level page keyboard event, so lv_async_call defers it until
     * it is safe to process in LVGL. This destroys the second-level page, restores the arrows,
     * restores the top-level entry styles, and returns keyboard focus to the top-level roller.
     */
    static void lvgl_back_this(void *p)
    {
        auto *self = static_cast<LvSettingRoller *>(p);
        if (!self) return;

        lv_group_t *group = self->input_group_;
        if (self->roller2_ && self->roller2_->Get()) {
            lv_group_remove_obj(self->roller2_->Get());
        }
        self->roller2_.reset();
        self->set_secondary_mode(false);

        if (self->ComponensObj && group) {
            lv_group_add_obj(group, self->ComponensObj);
            self->top_in_group_ = true;
            lv_group_focus_obj(self->ComponensObj);
        }
        self->AnimateNextOut(nullptr);
    }

    /**
     * Switch between the top-level page state and the second-level page sidebar state.
     *
     * When enabled is true, switch the top-level menu to the second-level page sidebar:
     * - All labels use a fixed width and left alignment;
     * - Enable marquee scrolling for the selected label;
     * - Hide the Up and Down arrows and show the right entry arrow.
     *
     * When enabled is false, perform the reverse operation:
     * - Restore the labels' content width and normal clipping mode;
     * - Recalculate the top-level menu styles from the current scroll position;
     * - Show the Up and Down arrows and hide the right entry arrow.
     */
    void set_secondary_mode(bool enabled = true)
    {
        secondary_active_ = enabled;
        if (ComponensObj) {
            lv_obj_update_layout(ComponensObj);
        }

        int item_index = 0;
        for (lv_obj_t *item_container : item_containers_) {
            if (!item_container) continue;

            lv_obj_t *label = lv_obj_get_child(item_container, 0);
            if (!label) continue;

            if (enabled) {
                lv_obj_set_width(label, LABEL_BOX_W);
                lv_label_set_long_mode(
                    label, item_index == selected_index
                        ? LV_LABEL_LONG_SCROLL_CIRCULAR
                        : LV_LABEL_LONG_CLIP);
                lv_obj_set_x(label, LABEL_BOX_X);
            } else {
                lv_obj_set_width(label, LV_SIZE_CONTENT);
                lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            }
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            ++item_index;
        }

        if (enabled) {
            if (arrow_up_) {
                lv_obj_add_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
            }
            if (arrow_down_) {
                lv_obj_add_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
            }
            if (right_arrow_) {
                lv_obj_remove_flag(right_arrow_, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (arrow_up_) {
                lv_obj_remove_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
            }
            if (arrow_down_) {
                lv_obj_remove_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
            }
            if (right_arrow_) {
                lv_obj_add_flag(right_arrow_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (ComponensObj) {
            lv_obj_send_event(ComponensObj, LV_EVENT_SCROLL, ComponensObj);
        }
    }
    void page3_transition_cb(bool entering, bool completed)
    {
        if (completed) {
            if (!entering && input_group_ && ComponensObj) {
                lv_group_add_obj(input_group_, ComponensObj);
                top_in_group_ = true;
            }
            page3_transitioning_ = false;
            return;
        }

        page3_transitioning_ = true;
        if (entering) {
            AnimateNextOut(nullptr);
        } else {
            AnimateNextIn(nullptr);
        }
    }

    static void page3_anim_exec_cb(void *object, int32_t value)
    {
        if (object) lv_obj_set_x(static_cast<lv_obj_t *>(object), value);
    }

    static void page3_animation_completed_cb(lv_anim_t *animation)
    {
        auto *self = static_cast<LvSettingRoller *>(lv_anim_get_user_data(animation));
        if (!self) return;

        auto animate_over_func = std::move(self->page3_animation_over_);
        self->page3_animation_over_ = nullptr;
        if (animate_over_func) animate_over_func();
    }

    void animate_page3_objects(bool entering, std::function<void()> animate_over_func)
    {
        page3_animation_over_ = nullptr;

        lv_obj_t *completion_object = ComponensObj ? ComponensObj : selection_bg_;
        if (!completion_object) {
            if (animate_over_func) animate_over_func();
            return;
        }

        animate_page3_object(selection_bg_, entering,
                             completion_object == selection_bg_ ? std::move(animate_over_func)
                                                                : nullptr);
        animate_page3_object(ComponensObj, entering,
                             completion_object == ComponensObj ? std::move(animate_over_func)
                                                                : nullptr);
        animate_page3_object(arrow_up_, entering,
                             completion_object == arrow_up_ ? std::move(animate_over_func)
                                                             : nullptr);
        animate_page3_object(arrow_down_, entering,
                             completion_object == arrow_down_ ? std::move(animate_over_func)
                                                               : nullptr);
        animate_page3_object(right_arrow_, entering,
                             completion_object == right_arrow_ ? std::move(animate_over_func)
                                                                : nullptr);
    }

    void animate_page3_object(lv_obj_t *object,
                              bool entering,
                              std::function<void()> animate_over_func = nullptr)
    {
        if (!object) return;

        lv_anim_del(object, nullptr);
        const int start_x = lv_obj_get_x(object);
        const int end_x = page3_object_base_x(object) + (entering ? -PAGE3_SHIFT : 0);

        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, object);
        lv_anim_set_values(&animation, start_x, end_x);
        lv_anim_set_time(&animation, PAGE3_ANIM_MS);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&animation, page3_anim_exec_cb);
        if (animate_over_func) {
            page3_animation_over_ = std::move(animate_over_func);
            lv_anim_set_user_data(&animation, this);
            lv_anim_set_completed_cb(&animation, page3_animation_completed_cb);
        }
        if (!lv_anim_start(&animation)) {
            lv_obj_set_x(object, end_x);
            page3_animation_completed_cb(&animation);
        }
    }

    int page3_object_base_x(lv_obj_t *object) const
    {
        if (object == selection_bg_) return selection_bg_base_x_;
        if (object == ComponensObj) return componens_base_x_;
        if (object == arrow_up_) return arrow_up_base_x_;
        if (object == arrow_down_) return arrow_down_base_x_;
        if (object == right_arrow_) return right_arrow_base_x_;
        return object ? lv_obj_get_x(object) : 0;
    }

    /**
     * Explicitly select a top-level entry from outside the component.
     *
     * The index is clamped to the valid range, followed by immediate non-animated scrolling and style refresh.
     */
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

    /**
     * Refresh all top-level entry styles from the current selected index.
     *
     * selected_index has already been updated before the scroll event is sent, so this directly uses
     * the entry index difference to calculate distance instead of relying on screen coordinates during scrolling.
     */
    static void scroll_event_cb(lv_event_t *e)
    {
        lv_obj_t *cont = lv_event_get_target(e);
        void *event_param = lv_event_get_param(e);
        auto *self = static_cast<LvSettingRoller *>(lv_event_get_user_data(e));
        if (!self) return;

        if (static_cast<void *>(self->ComponensObj) != event_param) {
            return;
        }

        if (!cont) return;

        uint32_t child_count = lv_obj_get_child_count(cont);
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t *child = lv_obj_get_child(cont, i);
            lv_obj_set_style_translate_x(child, 0, 0);
            lv_obj_t *label = lv_obj_get_child(child, 0);
            const int distance = std::abs(
                static_cast<int32_t>(i) - self->selected_index);
            style_label(label, distance, self && self->secondary_active_);
        }
    }

    /** Default constructor, primarily to keep the component interface complete. */
    LvSettingRoller() = default;

    // Parent container used when creating the second-level page.
    lv_obj_t *ui_APP_Container;

    /** Create a top-level roller and save the callback for returning to the parent page. */
    LvSettingRoller(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback)
        : parent_node_(parent_node), ui_APP_Container(parent)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
    }

    /**
     * Create all LVGL objects for the top-level roller:
     * - selection_bg：Background bar for the centered selection area;
     * - ComponensObj：Entry container supporting vertical scrolling and snapping;
     * - arrow_up_/arrow_down_：Up and Down hint arrows;
     * - right_arrow_：Right arrow shown when entering a second-level page;
     * - item_containers_：Storage for each top-level entry container, allowing styles to be adjusted together later.
     */
    void create_ui(lv_obj_t *parent) override
    {
        // Create the center highlight background. It does not accept input or scroll itself.
        selection_bg_ = lv_obj_create(parent);
        lv_obj_t *selection_bg = selection_bg_;
        lv_obj_set_size(selection_bg, 312, 21);
        lv_obj_set_pos(selection_bg, 4, 66);
        selection_bg_base_x_ = lv_obj_get_x(selection_bg);
        lv_obj_set_style_bg_color(selection_bg, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(selection_bg, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(selection_bg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(selection_bg, 0, LV_PART_MAIN);
        lv_obj_remove_flag(selection_bg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(selection_bg, LV_OBJ_FLAG_SCROLLABLE);

        // Create the scrolling container. Reserve three row heights above and below so the first and last entries can also snap to the center.
        ComponensObj = lv_obj_create(parent);
        lv_obj_set_size(ComponensObj, 320, 150);
        lv_obj_set_pos(ComponensObj, 0, 20);
        lv_obj_center(ComponensObj);
        componens_base_x_ = lv_obj_get_x(ComponensObj);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(ComponensObj, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_top(ComponensObj, EDGE_PADDING, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(ComponensObj, EDGE_PADDING, LV_PART_MAIN);
        lv_obj_set_style_pad_row(ComponensObj, 0, LV_PART_MAIN);

        // Create and position the scrolling hint arrows.
        arrow_up_ = lv_img_create(parent);

            lv_img_set_src(arrow_up_, &setting_red_up);
            lv_obj_update_layout(arrow_up_);
            lv_obj_set_pos(arrow_up_,
                           LABEL_CENTER_X - lv_obj_get_width(arrow_up_) / 2,
                           2);
            arrow_up_base_x_ = lv_obj_get_x(arrow_up_);


        arrow_down_ = lv_img_create(parent);

            lv_img_set_src(arrow_down_, &setting_red_down);
            lv_obj_update_layout(arrow_down_);
            lv_obj_set_pos(arrow_down_,
                           LABEL_CENTER_X - lv_obj_get_width(arrow_down_) / 2,
                           150 - lv_obj_get_height(arrow_down_) - 4);
            arrow_down_base_x_ = lv_obj_get_x(arrow_down_);
   

        // Show the right arrow only after entering a second-level page; hide it initially.
        right_arrow_ = lv_img_create(parent);
        lv_img_set_src(right_arrow_, &setting_right_arrow);
        lv_obj_set_pos(right_arrow_, 100, 65);
        right_arrow_base_x_ = lv_obj_get_x(right_arrow_);
        lv_obj_add_flag(right_arrow_, LV_OBJ_FLAG_HIDDEN);
        

        // Refresh each row's font and color while scrolling; forward key events to the member function through the bound callback.
        lv_obj_add_event_cb(ComponensObj, scroll_event_cb, LV_EVENT_SCROLL, this);
        // lv_obj_add_event_cb(ComponensObj, handle_key_event, LV_EVENT_KEY, NULL);
        DComponens::lvgl_bind_event(ComponensObj, LV_EVENT_KEY, NULL,
                                    std::bind(&LvSettingRoller::handle_key_event, this, std::placeholders::_1));
        lv_obj_set_style_radius(ComponensObj, 0, 0);
        lv_obj_set_style_clip_corner(ComponensObj, true, 0);
        lv_obj_set_scroll_dir(ComponensObj, LV_DIR_VER);
        lv_obj_set_scroll_snap_y(ComponensObj, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(ComponensObj, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
        // Create entries in NodeIter traversal order so the UI order matches the tree node order.
        item_containers_.clear();
        for (auto it = parent_node_.begin(); it != parent_node_.end(); ++it) {
            lv_obj_t *item_container = lv_obj_create(ComponensObj);
            item_containers_.push_back(item_container);
            lv_obj_set_size(item_container, 320, 21);
            lv_obj_set_style_bg_opa(item_container, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(item_container, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(item_container, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(item_container, 0, LV_PART_MAIN);
            lv_obj_remove_flag(item_container, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(item_container, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *label = lv_label_create(item_container);
            lv_label_set_text(label, it->label.c_str());
        }

        // Use the number of successfully created entry containers as the selectable item count.
        item_count_ = static_cast<uint32_t>(item_containers_.size());

        // After initial creation, position the first entry in the center and trigger a style refresh.
        selected_index = 2;
        
            scroll_to_selected(ComponensObj, false);
        
    }

private:
    static constexpr int PAGE3_ANIM_MS = 200;
    static constexpr int PAGE3_SHIFT = 320;

    // Tree node iterator for the current top-level page.
    NodeIter parent_node_;

    // Number of entries successfully created for the current top-level page.
    uint32_t item_count_ = 0;

    // true means the top-level roller is displayed as the second-level page sidebar.
    bool secondary_active_ = false;

    // Collection of all top-level entry containers, used to adjust Labels when entering or leaving a second-level page.
    std::list<lv_obj_t *> item_containers_;

    lv_group_t *input_group_ = nullptr;
    bool top_in_group_ = true;
    bool page3_transitioning_ = false;
    lv_obj_t *selection_bg_ = nullptr;
    int selection_bg_base_x_ = 0;
    int componens_base_x_ = 0;
    int arrow_up_base_x_ = 0;
    int arrow_down_base_x_ = 0;
    int right_arrow_base_x_ = 0;
    std::function<void()> page3_animation_over_ = nullptr;

    // Up arrow, Down arrow, and second-level page entry arrow.
    lv_obj_t *arrow_up_ = nullptr;
    lv_obj_t *arrow_down_ = nullptr;
    lv_obj_t *right_arrow_ = nullptr;
};
