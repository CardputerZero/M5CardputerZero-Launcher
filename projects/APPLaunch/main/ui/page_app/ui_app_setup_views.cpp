/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_setup.hpp"
#include "../model/setup_view_build_contract.hpp"

void UISetupPage::build_main_view()
{
    lv_obj_t *cont = ui_obj_["list_cont"];
    if (!cont) return;
    lv_obj_clean(cont);
    sel_bg_ = nullptr;
    hint_lbl_ = nullptr;
    arrow_up_obj_ = nullptr;
    arrow_down_obj_ = nullptr;
    for (auto &label : row_labels_) label = nullptr;

    SetupMainViewBuildContract build(ROWS_VISIBLE);
    auto rollback = [&]() {
        lv_obj_clean(cont);
        sel_bg_ = nullptr;
        hint_lbl_ = nullptr;
        arrow_up_obj_ = nullptr;
        arrow_down_obj_ = nullptr;
        for (auto &label : row_labels_) label = nullptr;
        lifecycle_.finish_animation();
    };

    int count = (int)menu_items_.size();
    static constexpr int SEL_BAR_H = 23;
    static constexpr int SEL_BAR_W = 312;
    sel_bg_ = lv_obj_create(cont);
    if (!sel_bg_) {
        rollback();
        return;
    }
    build.selection_created();
    lv_obj_set_size(sel_bg_, SEL_BAR_W, SEL_BAR_H);
    lv_obj_set_pos(sel_bg_, (SCREEN_W - SEL_BAR_W) / 2,
                   row_y(ROW_CENTER) + (row_h() - SEL_BAR_H) / 2);
    lv_obj_set_style_radius(sel_bg_, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sel_bg_, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sel_bg_, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(sel_bg_, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sel_bg_, LV_OBJ_FLAG_SCROLLABLE);

    hint_lbl_ = lv_label_create(cont);
    if (!hint_lbl_) {
        rollback();
        return;
    }
    build.hint_created();
    lv_label_set_text(hint_lbl_, "ok:enter");
    lv_obj_set_style_text_color(hint_lbl_, lv_color_hex(0x00CC66), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint_lbl_,
                               launcher_fonts().get("Montserrat-Bold.ttf", 16,
                                                    LV_FREETYPE_FONT_STYLE_BOLD),
                               LV_PART_MAIN);
    lv_obj_update_layout(hint_lbl_);
    int hint_w = lv_obj_get_width(hint_lbl_);
    int hint_h = lv_obj_get_height(hint_lbl_);
    lv_obj_set_pos(hint_lbl_, SCREEN_W - 6 - hint_w,
                   row_y(ROW_CENTER) + (row_h() - hint_h) / 2);

    for (int vi = 0; vi < ROWS_VISIBLE; ++vi) {
        int item_idx = selected_idx_ - ROW_CENTER + vi;
        row_labels_[vi] = create_menu_label(cont, vi, item_idx, count);
        if (!row_labels_[vi]) {
            rollback();
            return;
        }
        build.row_created();
    }

    if (!build.ready()) {
        rollback();
        return;
    }

    int arrow_x = MENU_X - 8;
    arrow_up_obj_ = lv_img_create(cont);
    if (arrow_up_obj_) {
        lv_img_set_src(arrow_up_obj_, img_arrow_up_.c_str());
        lv_obj_set_pos(arrow_up_obj_, arrow_x, 2);
    }

    arrow_down_obj_ = lv_img_create(cont);
    if (arrow_down_obj_) {
        lv_img_set_src(arrow_down_obj_, img_arrow_down_.c_str());
        lv_obj_set_pos(arrow_down_obj_, arrow_x, LIST_H - 14);
    }
    lifecycle_.finish_animation();
}

void UISetupPage::animate_scroll(int direction)
{
    int count = (int)menu_items_.size();
    if (direction < 0) bounce_arrow(arrow_up_obj_, -1);
    else bounce_arrow(arrow_down_obj_, 1);

    if (lifecycle_.animating() || !model_.move_main(direction, count)) return;
    if (!lifecycle_.begin_animation()) return;

    for (int vi = 0; vi < ROWS_VISIBLE; ++vi) {
        if (!row_labels_[vi]) continue;
        int item_idx = selected_idx_ - ROW_CENTER + vi;
        if (item_idx >= 0 && item_idx < count) {
            lv_label_set_text(row_labels_[vi], menu_items_[item_idx].label.c_str());
            lv_obj_clear_flag(row_labels_[vi], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(row_labels_[vi], "");
            lv_obj_add_flag(row_labels_[vi], LV_OBJ_FLAG_HIDDEN);
        }
    }

    int rh = row_h();
    int offset = direction * rh;
    bool completion_scheduled = false;
    for (int vi = 0; vi < ROWS_VISIBLE; ++vi) {
        if (!row_labels_[vi]) continue;
        RowStyle style = style_for_slot(vi);
        int font_h = lv_font_get_line_height(style.font);
        int target_y = row_y(vi) + (rh - font_h) / 2;

        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, row_labels_[vi]);
        lv_anim_set_exec_cb(&animation, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_values(&animation, target_y + offset, target_y);
        lv_anim_set_time(&animation, ANIM_TIME);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        if (vi == ROW_CENTER) {
            lv_anim_set_completed_cb(&animation, anim_done_cb);
            lv_anim_set_user_data(&animation, this);
        }
        lv_anim_t *started = lv_anim_start(&animation);
        if (vi == ROW_CENTER) completion_scheduled = started != nullptr;

        lv_obj_set_style_text_color(row_labels_[vi], lv_color_hex(style.color), LV_PART_MAIN);
        lv_obj_set_style_text_font(row_labels_[vi], style.font, LV_PART_MAIN);
        lv_obj_set_style_opa(row_labels_[vi], style.opa, LV_PART_MAIN);
        lv_obj_update_layout(row_labels_[vi]);
        int label_x = MENU_X - lv_obj_get_width(row_labels_[vi]) / 2;
        lv_obj_set_x(row_labels_[vi], label_x < 4 ? 4 : label_x);
    }
    if (!completion_scheduled) lifecycle_.finish_animation();
}

void UISetupPage::cancel_scroll_animations()
{
    lifecycle_.cancel_animation();
    for (auto *label : row_labels_) {
        if (label) lv_anim_del(label, nullptr);
    }
}

void UISetupPage::anim_done_cb(lv_anim_t *animation) noexcept
{
    auto *self = static_cast<UISetupPage *>(lv_anim_get_user_data(animation));
    if (self) self->lifecycle_.finish_animation();
}

void UISetupPage::rebuild_view()
{
    input_context_scope_.update(setup_input_context(view_state_));
    if (view_state_ == ViewState::MAIN) build_main_view();
    else if (view_state_ == ViewState::SUB) build_sub_view();
    else if (view_state_ == ViewState::VALUE_SELECT) build_value_view();
    else if (view_state_ == ViewState::HELP) setting::Help::build_page(*this);
    else if (view_state_ == ViewState::WIFI_LIST) wifi_.build_list(*this);
    else if (view_state_ == ViewState::BT_LIST) bluetooth_.build_list(*this);
    else if (view_state_ == ViewState::BT_ALIAS) bluetooth_.build_alias_view(*this);
    else if (view_state_ == ViewState::SOUNDCARD_CARDS) soundcard_.build_cards_view(*this);
    else if (view_state_ == ViewState::SOUNDCARD_CONTROLS) soundcard_.build_controls_view(*this);
    else if (view_state_ == ViewState::SOUNDCARD_DETAIL) soundcard_.build_detail_view(*this);
    else if (view_state_ == ViewState::ADB_PAIR) developer_.enter_pair_view(*this);
    else if (view_state_ == ViewState::ADB_AUTHORIZATIONS)
        developer_.build_authorizations_view(*this);
}

void UISetupPage::bounce_arrow(lv_obj_t *arrow, int direction)
{
    if (!arrow) return;
    int current_y = lv_obj_get_y(arrow);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, arrow);
    lv_anim_set_exec_cb(&animation, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&animation, current_y + direction * 4, current_y);
    lv_anim_set_time(&animation, 150);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void UISetupPage::slide_transition(int direction)
{
    lv_obj_t *background = ui_obj_["bg"];
    lv_obj_t *old_container = ui_obj_["list_cont"];
    if (!background || !old_container) {
        rebuild_view();
        return;
    }

    lv_obj_t *new_container = lv_obj_create(background);
    if (!new_container) {
        rebuild_view();
        return;
    }
    lv_obj_set_size(new_container, SCREEN_W, LIST_H);
    lv_obj_set_pos(new_container, direction * SCREEN_W, 0);
    lv_obj_set_style_radius(new_container, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(new_container, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(new_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(new_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(new_container, LV_OBJ_FLAG_SCROLLABLE);

    ui_obj_["list_cont"] = new_container;
    rebuild_view();

    lv_anim_t old_animation;
    lv_anim_init(&old_animation);
    lv_anim_set_var(&old_animation, old_container);
    lv_anim_set_exec_cb(&old_animation, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_values(&old_animation, 0, -direction * SCREEN_W);
    lv_anim_set_time(&old_animation, 200);
    lv_anim_set_path_cb(&old_animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&old_animation, slide_old_done_cb);
    lv_anim_set_user_data(&old_animation, old_container);
    if (!lv_anim_start(&old_animation))
        lv_obj_del(old_container);

    lv_anim_t new_animation;
    lv_anim_init(&new_animation);
    lv_anim_set_var(&new_animation, new_container);
    lv_anim_set_exec_cb(&new_animation, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_values(&new_animation, direction * SCREEN_W, 0);
    lv_anim_set_time(&new_animation, 200);
    lv_anim_set_path_cb(&new_animation, lv_anim_path_ease_out);
    if (!lv_anim_start(&new_animation))
        lv_obj_set_x(new_container, 0);
}

void UISetupPage::slide_old_done_cb(lv_anim_t *animation) noexcept
{
    auto *old_container = static_cast<lv_obj_t *>(lv_anim_get_user_data(animation));
    if (old_container) lv_obj_del(old_container);
}

void UISetupPage::transition_enter_level()
{
    slide_transition(1);
}

void UISetupPage::transition_back_level()
{
    slide_transition(-1);
}

void UISetupPage::build_sub_view()
{
    lv_obj_t *container = ui_obj_["list_cont"];
    if (!container || selected_idx_ < 0 ||
        selected_idx_ >= static_cast<int>(menu_items_.size())) return;
    info_.reset_visible_labels();
    lv_obj_clean(container);

    MenuItem &item = menu_items_[selected_idx_];
    int sub_count = static_cast<int>(item.sub_items.size());
    int menu_count = static_cast<int>(menu_items_.size());

    static constexpr int SEL_BAR_H = 23;
    static constexpr int SEL_BAR_W = 312;
    lv_obj_t *bar = lv_obj_create(container);
    if (!bar) return;
    lv_obj_set_size(bar, SEL_BAR_W, SEL_BAR_H);
    lv_obj_set_pos(bar, (SCREEN_W - SEL_BAR_W) / 2,
                   row_y(ROW_CENTER) + (row_h() - SEL_BAR_H) / 2);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    for (int vi = 0; vi < ROWS_VISIBLE; ++vi) {
        int item_index = selected_idx_ - ROW_CENTER + vi;
        if (item_index < 0 || item_index >= menu_count) continue;
        lv_obj_t *label = create_menu_label(container, vi, item_index, menu_count);
        clamp_label_box(label, SUB_LEFT_BOX_X, SUB_LEFT_BOX_W, vi == ROW_CENTER);
    }

    if (sub_count == 0) {
        create_carousel_label(container, ROW_CENTER, ROW_CENTER,
                              "(no options)", SUB_CENTER_X, true);
        return;
    }
    if (sub_selected_idx_ < 0 || sub_selected_idx_ >= sub_count) return;

    struct SubLabelInfo {
        lv_obj_t *label;
        int sub_index;
    };
    SubLabelInfo sub_labels[ROWS_VISIBLE] = {};
    int sub_label_count = 0;

    for (int vi = 0; vi < ROWS_VISIBLE; ++vi) {
        int sub_index = sub_selected_idx_ - ROW_CENTER + vi;
        if (sub_index < 0 || sub_index >= sub_count) continue;

        SubItem &sub = item.sub_items[sub_index];
        lv_obj_t *label = create_carousel_label(container, vi, ROW_CENTER,
                                                 sub.label.c_str(), SUB_CENTER_X, true);
        if (!label) continue;
        bool focused = vi == ROW_CENTER;
        apply_overflow_centered(label, SUB_CENTER_X, focused ? 80 : VALUE_BOX_W, focused);
        if (item.label == "Info" && sub_index < 4)
            info_.track_visible_label(sub_index, label, focused, sub.label);
        sub_labels[sub_label_count++] = {label, sub_index};
    }

    static constexpr int INDICATOR_X = 220;
    for (int i = 0; i < sub_label_count; ++i) {
        SubItem &sub = item.sub_items[sub_labels[i].sub_index];
        if (!sub.is_toggle) continue;

        lv_obj_t *label = sub_labels[i].label;
        if (!label) continue;
        lv_obj_t *indicator = lv_img_create(container);
        if (!indicator) continue;
        lv_img_set_src(indicator, sub.toggle_state ? img_ok_.c_str() : img_cross_.c_str());
        lv_obj_update_layout(indicator);
        int x_offset = sub.toggle_state ? 0 : 1;
        int indicator_y = lv_obj_get_y(label) +
            (lv_obj_get_height(label) - lv_obj_get_height(indicator)) / 2;
        lv_obj_set_pos(indicator, INDICATOR_X + x_offset, indicator_y);
    }

    place_fixed_sub_arrow(container);

    int arrow_x = SUB_CENTER_X - 8;
    if (sub_selected_idx_ > 0) {
        lv_obj_t *arrow = lv_img_create(container);
        if (arrow) {
            lv_img_set_src(arrow, img_arrow_up_.c_str());
            lv_obj_set_pos(arrow, arrow_x, 2);
        }
    }
    if (sub_selected_idx_ < sub_count - 1) {
        lv_obj_t *arrow = lv_img_create(container);
        if (arrow) {
            lv_img_set_src(arrow, img_arrow_down_.c_str());
            lv_obj_set_pos(arrow, arrow_x, LIST_H - 14);
        }
    }

    SubItem &current_sub = item.sub_items[sub_selected_idx_];
    lv_obj_t *hint = lv_label_create(container);
    if (!hint) return;
    if (current_sub.is_toggle && item.label == "RTC" && current_sub.label == "NTP")
        lv_label_set_text(hint, rtc_.ntp_available()
                                  ? (current_sub.toggle_state ? "ok:disable" : "ok:enable")
                                  : "unavailable");
    else if (current_sub.is_toggle && item.label == "WiFi" && current_sub.label == "Power")
        lv_label_set_text(hint, current_sub.toggle_state ? "ok:disable" : "ok:enable");
    else if (current_sub.is_toggle && item.label == "Bluetooth" &&
             current_sub.label == "Named Only")
        lv_label_set_text(hint, current_sub.toggle_state ? "ok:show all" : "ok:named");
    else if (current_sub.is_toggle)
        lv_label_set_text(hint, current_sub.toggle_state ? "ok:hide" : "ok:show");
    else if (item.label == "RTC" && rtc_.ntp_on())
        lv_label_set_text(hint, "ntp on");
    else
        lv_label_set_text(hint, "ok:enter");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x00CC66), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint,
                               launcher_fonts().get("Montserrat-Bold.ttf", 16,
                                                    LV_FREETYPE_FONT_STYLE_BOLD),
                               LV_PART_MAIN);
    apply_right_hint_overflow(hint, row_y(ROW_CENTER));
}

void UISetupPage::apply_right_hint_overflow(lv_obj_t *hint, int row_top_y)
{
    if (!hint) return;
    lv_obj_update_layout(hint);
    int hint_width = lv_obj_get_width(hint);
    int y = row_top_y + (row_h() - lv_obj_get_height(hint)) / 2;
    if (hint_width > RIGHT_HINT_BOX_W) {
        lv_obj_set_width(hint, RIGHT_HINT_BOX_W);
        lv_obj_set_pos(hint, SCREEN_W - 6 - RIGHT_HINT_BOX_W, y);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_SCROLL_CIRCULAR);
    } else {
        lv_obj_set_pos(hint, SCREEN_W - 6 - hint_width, y);
    }
}

void UISetupPage::build_value_view()
{
    lv_obj_t *container = ui_obj_["list_cont"];
    if (!container || selected_idx_ < 0 ||
        selected_idx_ >= static_cast<int>(menu_items_.size())) return;
    lv_obj_clean(container);

    int sub_count = static_cast<int>(menu_items_[selected_idx_].sub_items.size());
    int value_count = static_cast<int>(val_options_.size());
    static constexpr int SEL_BAR_H = 22;
    static constexpr int SEL_BAR_W = 312;
    lv_obj_t *bar = lv_obj_create(container);
    if (!bar) return;
    lv_obj_set_size(bar, SEL_BAR_W, SEL_BAR_H);
    lv_obj_set_pos(bar, (SCREEN_W - SEL_BAR_W) / 2,
                   row_y(ROW_CENTER) + (row_h() - SEL_BAR_H) / 2);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    static constexpr int LEFT_BOX_W = 84;
    lv_obj_t *center_label = nullptr;
    for (int vi = 0; vi < ROWS_VISIBLE; ++vi) {
        int sub_index = sub_selected_idx_ - ROW_CENTER + vi;
        if (sub_index < 0 || sub_index >= sub_count) continue;
        const char *text = menu_items_[selected_idx_].sub_items[sub_index].label.c_str();
        lv_obj_t *label = create_carousel_label(container, vi, ROW_CENTER, text, MENU_X);
        apply_overflow_centered(label, MENU_X, LEFT_BOX_W, vi == ROW_CENTER);
        if (vi == ROW_CENTER) center_label = label;
    }

    static constexpr int VALUE_CENTER_X = 160;
    int right_min_x = SCREEN_W;
    for (int vi = 0; vi < ROWS_VISIBLE; ++vi) {
        int value_index = val_sel_idx_ - ROW_CENTER + vi;
        if (value_index < 0 || value_index >= value_count) continue;
        lv_obj_t *label = create_carousel_label(container, vi, ROW_CENTER,
                                                 val_options_[value_index].c_str(),
                                                 VALUE_CENTER_X, true);
        if (!label) continue;
        apply_overflow_centered(label, VALUE_CENTER_X, VALUE_BOX_W, vi == ROW_CENTER);
        lv_obj_update_layout(label);
        if (lv_obj_get_x(label) < right_min_x) right_min_x = lv_obj_get_x(label);
    }

    if (center_label && value_count > 0)
        place_blue_arrow(container, center_label, right_min_x);

    int arrow_x = VALUE_CENTER_X - 8;
    if (val_sel_idx_ > 0) {
        lv_obj_t *arrow = lv_img_create(container);
        if (arrow) {
            lv_img_set_src(arrow, img_arrow_up_.c_str());
            lv_obj_set_pos(arrow, arrow_x, 2);
        }
    }
    if (val_sel_idx_ < value_count - 1) {
        lv_obj_t *arrow = lv_img_create(container);
        if (arrow) {
            lv_img_set_src(arrow, img_arrow_down_.c_str());
            lv_obj_set_pos(arrow, arrow_x, LIST_H - 14);
        }
    }

    lv_obj_t *hint = lv_label_create(container);
    if (!hint) return;
    lv_label_set_text(hint, "ok:set");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x00CC66), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint,
                               launcher_fonts().get("Montserrat-Bold.ttf", 16,
                                                    LV_FREETYPE_FONT_STYLE_BOLD),
                               LV_PART_MAIN);
    lv_obj_update_layout(hint);
    lv_obj_set_pos(hint, SCREEN_W - 6 - lv_obj_get_width(hint),
                   row_y(ROW_CENTER) + (row_h() - lv_obj_get_height(hint)) / 2);
}
