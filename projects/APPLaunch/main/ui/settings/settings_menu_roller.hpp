/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdio>
#include <iterator>
#include <list>
#include <memory>

#include "settings_fonts.hpp"
#include "cp0_lvgl_app_page_assets.h"
#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY
#include "settings_tree_types.hpp"

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
 * is arranged by ComponensObj's vertical Flex layout, while this class manually controls the Label's horizontal
 * position to match the settings-page alignment used by APPLaunch.
 */
class LvSettingRoller : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        RowH         = 21,
        CenterRow    = 3,
        EdgePadding  = RowH * CenterRow,
        LabelCenterX = 60,
        LabelBoxX    = 4,
        LabelBoxW    = 90,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    // Index of the currently selected top-level entry.
    int32_t selected_index = 0;

    // Second-level roller object; created when entering a second-level page and destroyed when returning.
    std::unique_ptr<DComponens::LvglComponensBase> roller2_ = nullptr;

    ~LvSettingRoller() override
    {
        cancel_async_tasks();
        roller2_.reset();
    }

    void AnimateNextIn(std::function<void()> animate_over_func) override
    {
        slide_page_objects(false, std::move(animate_over_func));
    }

    void AnimateNextOut(std::function<void()> animate_over_func) override
    {
        slide_page_objects(true, std::move(animate_over_func));
    }

    void LoadNextPage() override
    {
        if (item_count_ == 0 || selected_index < 0 || selected_index >= static_cast<int32_t>(item_count_)) {
            return;
        }
        if (roller2_) return;

        auto selected_node = std::next(parent_node_.begin(), selected_index);
        if (!selected_node->page_factory) return;

        SetSelfUiMode(selected_node->page_type);
        input_group_ = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr;
        if (ComponensObj && input_group_) {
            lv_group_remove_obj(ComponensObj);
            top_in_group_ = false;
        }

        roller2_ = selected_node->page_factory(ui_APP_Container, selected_node,
                                               std::bind(&LvSettingRoller::LeaveNextPage, this));
        if (!roller2_ || !roller2_->Get()) {
            roller2_.reset();
            SetSelfUiMode(PageType::Normal);
            if (ComponensObj && input_group_) {
                lv_group_add_obj(input_group_, ComponensObj);
                top_in_group_ = true;
                lv_group_focus_obj(ComponensObj);
            }
            return;
        }
        AnimateNextIn([this]() {
            if (!input_group_ || !roller2_ || !roller2_->Get()) return;

            auto *page = roller2_->Get();
            lv_group_add_obj(input_group_, page);
            lv_group_focus_obj(page);
        });
    }

    void LeaveNextPage() override
    {
        if (leave_pending_.exchange(true, std::memory_order_acq_rel)) return;
        if (!enqueue_async([this] { lvgl_back_this(this); })) leave_pending_.store(false, std::memory_order_release);
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
            label, settings_fonts::sans(font_size, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_opa(label, opa, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        // Restore the content width and disable scrolling first to obtain the text's natural width.
        // Re-enable marquee scrolling only when the entry is selected and the text actually exceeds the display box.
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(label);

        const bool focused      = distance == 0;
        const int natural_width = lv_obj_get_width(label);
        if (left_aligned) {
            // Top-level entries on the left side of a second-level page use a fixed width and left alignment.
            lv_obj_set_width(label, metric(LayoutMetric::LabelBoxW));
            lv_label_set_long_mode(label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, metric(LayoutMetric::LabelBoxX));
        } else {
            // Normal entries that do not exceed the width are centered around the midpoint without crossing the minimum
            // left margin.
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_update_layout(label);
            const int label_x = metric(LayoutMetric::LabelCenterX) - lv_obj_get_width(label) / 2;
            lv_obj_set_x(label, label_x < metric(LayoutMetric::LabelBoxX) ? metric(LayoutMetric::LabelBoxX) : label_x);
        }

        // Vertically center the text within the 21-pixel row; if the font is too tall, start at the top.
        const int label_y = (metric(LayoutMetric::RowH) - lv_obj_get_height(label)) / 2;
        lv_obj_set_y(label, label_y < 0 ? 0 : label_y);
    }

    /**
     * Scroll the entry at the specified index into the centered selection area.
     *
     * Use LVGL animation when animated is true; during initial creation, explicit selection, or cyclic
     * wraparound, positioning is normally done without animation to avoid unnatural long-distance scrolling between the
     * ends.
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
     * The callback may originate from a second-level page keyboard event, so the dispatch timer defers it until
     * it is safe to process in LVGL. This destroys the second-level page, restores the arrows,
     * restores the top-level entry styles, and returns keyboard focus to the top-level roller.
     */
    static void lvgl_back_this(void *p)
    {
        auto *self = static_cast<LvSettingRoller *>(p);
        if (!self) return;
        self->leave_pending_.store(false, std::memory_order_release);

        lv_group_t *group = self->input_group_;
        if (self->roller2_ && self->roller2_->Get()) {
            lv_group_remove_obj(self->roller2_->Get());
        }
        self->roller2_.reset();
        self->SetSelfUiMode(PageType::Normal);

        // A child page may update its tree node asynchronously (for example,
        // Bluetooth Alias is resolved from BtStatus after the page opens).
        // The roller labels are snapshots created in create_ui(), so refresh
        // their text before handing focus back to the parent view.
        auto container = self->item_containers_.begin();
        for (auto node = self->parent_node_.begin();
             node != self->parent_node_.end() && container != self->item_containers_.end();
             ++node, ++container) {
            lv_obj_t *item_container = *container;
            lv_obj_t *label = item_container ? lv_obj_get_child(item_container, 0) : nullptr;
            if (label) lv_label_set_text(label, node->label.c_str());
        }

        if (self->ComponensObj && group) {
            lv_group_add_obj(group, self->ComponensObj);
            self->top_in_group_ = true;
            lv_group_focus_obj(self->ComponensObj);
        }
#if 0
        self->AnimateNextOut(nullptr);
#endif
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
    void SetSelfUiMode(PageType mode) override
    {
        if (mode == PageType::FullCustom) return;

        const bool enabled = mode == PageType::NextPageNeeded;
        NextActive = enabled;
        if (ComponensObj) {
            lv_obj_update_layout(ComponensObj);
        }

        int item_index = 0;
        for (lv_obj_t *item_container : item_containers_) {
            if (!item_container) continue;

            lv_obj_t *label = lv_obj_get_child(item_container, 0);
            if (!label) continue;

            if (enabled) {
                lv_obj_set_width(label, metric(LayoutMetric::LabelBoxW));
                lv_label_set_long_mode(
                    label, item_index == selected_index ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
                lv_obj_set_x(label, metric(LayoutMetric::LabelBoxX));
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
        } else {
            if (arrow_up_) {
                lv_obj_remove_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
            }
            if (arrow_down_) {
                lv_obj_remove_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (ComponensObj) {
            lv_obj_send_event(ComponensObj, LV_EVENT_SCROLL, ComponensObj);
        }
    }
    static void page_anim_exec_cb(void *object, int32_t value)
    {
        if (object) lv_obj_set_x(static_cast<lv_obj_t *>(object), value);
    }

    static void page_animation_completed_cb(lv_anim_t *animation)
    {
        auto *self = static_cast<LvSettingRoller *>(lv_anim_get_user_data(animation));
        if (!self) return;

        auto animate_over_func     = std::move(self->page_animation_over_);
        self->page_animation_over_ = nullptr;
        if (animate_over_func) animate_over_func();
    }

    /**
     * Slide the page's objects (selection bar and roller container) to or
     * from their resting positions to perform a page transition.
     *
     * @param leaving          When true, slide the whole page one screen-width
     *                         off-screen to the left (page slides away);
     *                         when false, slide it back to its resting position.
     * @param animate_over_func Completion callback fired once the slide finishes.
     */
    void slide_page_objects(bool leaving, std::function<void()> animate_over_func)
    {
        page_animation_over_ = nullptr;

        lv_obj_t *completion_object = ComponensObj ? ComponensObj : selection_bg_;
        if (!completion_object) {
            if (animate_over_func) animate_over_func();
            return;
        }

        slide_page_object(selection_bg_, leaving,
                          completion_object == selection_bg_ ? std::move(animate_over_func) : nullptr);
        slide_page_object(ComponensObj, leaving,
                          completion_object == ComponensObj ? std::move(animate_over_func) : nullptr);
    }

    void slide_page_object(lv_obj_t *object, bool leaving, std::function<void()> animate_over_func = nullptr)
    {
        if (!object) return;

        lv_anim_del(object, nullptr);
        const int start_x = lv_obj_get_x(object);
        const int end_x   = page_object_base_x(object) + (leaving ? -animation_metric(AnimationMetric::PageShift) : 0);

        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, object);
        lv_anim_set_values(&animation, start_x, end_x);
        lv_anim_set_time(&animation, animation_metric(AnimationMetric::PageAnimMs));
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&animation, page_anim_exec_cb);
        if (animate_over_func) {
            page_animation_over_ = std::move(animate_over_func);
            lv_anim_set_user_data(&animation, this);
            lv_anim_set_completed_cb(&animation, page_animation_completed_cb);
        }
        if (!lv_anim_start(&animation)) {
            lv_obj_set_x(object, end_x);
            page_animation_completed_cb(&animation);
        }
    }

    int page_object_base_x(lv_obj_t *object) const
    {
        if (object == selection_bg_) return selection_bg_base_x_;
        if (object == ComponensObj) return componens_base_x_;
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
        lv_obj_t *cont    = lv_event_get_target(e);
        void *event_param = lv_event_get_param(e);
        auto *self        = static_cast<LvSettingRoller *>(lv_event_get_user_data(e));
        if (!self) return;

        if (static_cast<void *>(self->ComponensObj) != event_param) {
            return;
        }

        if (!cont) return;

        uint32_t child_count = lv_obj_get_child_count(cont);
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t *child = lv_obj_get_child(cont, i);
            lv_obj_set_style_translate_x(child, 0, 0);
            lv_obj_t *label    = lv_obj_get_child(child, 0);
            const int distance = std::abs(static_cast<int32_t>(i) - self->selected_index);
            style_label(label, distance, self && self->NextActive);
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
     * - item_containers_：Storage for each top-level entry container, allowing styles to be adjusted together later.
     */
    void create_ui(lv_obj_t *parent) override
    {
        ensure_async_dispatch();
        lv_obj_t *page_container = lv_obj_create(parent);
        if (!page_container) return;
        lv_obj_set_size(page_container, 320, 150);
        lv_obj_set_pos(page_container, 0, 0);
        lv_obj_set_style_bg_opa(page_container, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(page_container, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(page_container, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(page_container, 0, LV_PART_MAIN);
        lv_obj_remove_flag(page_container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);

        // Create the center highlight background. It does not accept input or scroll itself.
        selection_bg_          = lv_obj_create(page_container);
        lv_obj_t *selection_bg = selection_bg_;
        if (!selection_bg) return;
        lv_obj_set_size(selection_bg, 312, 21);
        lv_obj_set_pos(selection_bg, 4, 66);
        selection_bg_base_x_ = lv_obj_get_x(selection_bg);
        lv_obj_set_style_bg_color(selection_bg, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(selection_bg, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(selection_bg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(selection_bg, 0, LV_PART_MAIN);
        lv_obj_remove_flag(selection_bg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(selection_bg, LV_OBJ_FLAG_SCROLLABLE);

        // Create the scrolling container. Reserve three row heights above and below so the first and last entries can
        // also snap to the center.
        ComponensObj = lv_obj_create(page_container);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj, 320, 150);
        lv_obj_set_pos(ComponensObj, 0, 20);
        lv_obj_center(ComponensObj);
        componens_base_x_ = lv_obj_get_x(ComponensObj);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(ComponensObj, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_top(ComponensObj, metric(LayoutMetric::EdgePadding), LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(ComponensObj, metric(LayoutMetric::EdgePadding), LV_PART_MAIN);
        lv_obj_set_style_pad_row(ComponensObj, 0, LV_PART_MAIN);

        // Create and position the scrolling hint arrows.
        arrow_up_ = lv_img_create(page_container);

        lv_img_set_src(arrow_up_, &setting_red_up);
        lv_obj_update_layout(arrow_up_);
        lv_obj_set_pos(arrow_up_, metric(LayoutMetric::LabelCenterX) - lv_obj_get_width(arrow_up_) / 2, 2);

        arrow_down_ = lv_img_create(page_container);

        lv_img_set_src(arrow_down_, &setting_red_down);
        lv_obj_update_layout(arrow_down_);
        lv_obj_set_pos(arrow_down_, metric(LayoutMetric::LabelCenterX) - lv_obj_get_width(arrow_down_) / 2,
                       150 - lv_obj_get_height(arrow_down_) - 4);

        // Refresh each row's font and color while scrolling; forward key events to the member function through the
        // bound callback.
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
            selected_index     = wrapped ? static_cast<int32_t>(item_count_ - 1) : selected_index - 1;
            scroll_to_selected(cont, !wrapped);
        } else if (key == LV_KEY_DOWN) {
            // After reaching the last entry, continue from the first entry.
            const bool wrapped = selected_index == static_cast<int32_t>(item_count_ - 1);
            selected_index     = wrapped ? 0 : selected_index + 1;
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

private:
    enum class AnimationMetric : int {
        PageAnimMs = 200,
        PageShift  = 320,
    };

    static constexpr int animation_metric(AnimationMetric value)
    {
        return static_cast<int>(value);
    }

    // Tree node iterator for the current top-level page.
    NodeIter parent_node_;

    // Number of entries successfully created for the current top-level page.
    uint32_t item_count_ = 0;

    // Collection of all top-level entry containers, used to adjust Labels when entering or leaving a second-level page.
    std::list<lv_obj_t *> item_containers_;

    lv_group_t *input_group_                   = nullptr;
    bool top_in_group_                         = true;
    lv_obj_t *selection_bg_                    = nullptr;
    int selection_bg_base_x_                   = 0;
    int componens_base_x_                      = 0;
    std::function<void()> page_animation_over_ = nullptr;
    std::atomic_bool leave_pending_{false};

    // Up and Down scrolling hint arrows.
    lv_obj_t *arrow_up_   = nullptr;
    lv_obj_t *arrow_down_ = nullptr;
};
