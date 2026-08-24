/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_setup.hpp"

void UISetupPage::create_ui()
{
    if (!ui_APP_Container) return;
    lv_obj_t *bg = lv_obj_create(ui_APP_Container);
    if (!bg) return;
    lv_obj_set_size(bg, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    ui_obj_["bg"] = bg;

    lv_obj_t *list = lv_obj_create(bg);
    if (!list) {
        lv_obj_del(bg);
        ui_obj_.erase("bg");
        return;
    }
    lv_obj_set_size(list, SCREEN_W, LIST_H);
    lv_obj_set_pos(list, 0, 0);
    lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    ui_obj_["list_cont"] = list;
    build_main_view();
}

int UISetupPage::row_h() const
{
    return LIST_H / ROWS_VISIBLE;
}

int UISetupPage::row_y(int visual_index) const
{
    const int center_y = (LIST_H - row_h()) / 2;
    return center_y + (visual_index - ROW_CENTER) * row_h();
}

UISetupPage::RowStyle UISetupPage::style_for_slot(int visual_index)
{
    const int distance = visual_index > ROW_CENTER
        ? visual_index - ROW_CENTER
        : ROW_CENTER - visual_index;
    if (distance == 0)
        return {launcher_fonts().get("Montserrat-Bold.ttf", 18, LV_FREETYPE_FONT_STYLE_BOLD), 0xFFFFFF, MENU_X, 255};
    if (distance == 1)
        return {launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD), 0xAAAAAA, MENU_X, 220};
    if (distance == 2)
        return {launcher_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD), 0x777777, MENU_X, 170};
    return {&lv_font_montserrat_12, 0x555555, MENU_X, 130};
}

lv_obj_t *UISetupPage::create_carousel_label(lv_obj_t *parent, int vi, int center_vi,
                                              const char *text, int center_x, bool smaller)
{
    if (!parent) return nullptr;
    int dist = vi > center_vi ? vi - center_vi : center_vi - vi;
    const lv_font_t *font;
    uint32_t color;
    int opa;
    if (!smaller) {
        if (dist == 0) {
            font = launcher_fonts().get("Montserrat-Bold.ttf", 18, LV_FREETYPE_FONT_STYLE_BOLD);
            color = 0xFFFFFF; opa = 255;
        } else if (dist == 1) {
            font = launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD);
            color = 0xAAAAAA; opa = 220;
        } else if (dist == 2) {
            font = launcher_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD);
            color = 0x777777; opa = 170;
        } else {
            font = &lv_font_montserrat_12;
            color = 0x555555; opa = 130;
        }
    } else {
        if (dist == 0) {
            font = launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD);
            color = 0xFFFFFF; opa = 255;
        } else if (dist == 1) {
            font = launcher_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD);
            color = 0xAAAAAA; opa = 220;
        } else if (dist == 2) {
            font = &lv_font_montserrat_12;
            color = 0x777777; opa = 170;
        } else {
            font = &lv_font_montserrat_10;
            color = 0x555555; opa = 130;
        }
    }

    lv_obj_t *lbl = lv_label_create(parent);
    if (!lbl) return nullptr;
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_opa(lbl, opa, LV_PART_MAIN);

    lv_obj_update_layout(lbl);
    int tw = lv_obj_get_width(lbl);
    int lx = center_x - tw / 2;
    if (lx < 4) lx = 4;
    int font_h = lv_font_get_line_height(font);
    int ly = row_y(vi) + (row_h() - font_h) / 2;
    if (smaller) ly += 1;
    lv_obj_set_pos(lbl, lx, ly);

    if (!text || !text[0]) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    return lbl;
}

void UISetupPage::place_blue_arrow(lv_obj_t *parent, lv_obj_t *left_lbl, int right_min_x)
{
    if (!left_lbl || right_min_x <= 0) return;
    lv_obj_update_layout(left_lbl);

    const int vis = ARROW_SRC * ARROW_SCALE / 256;
    int left_right_edge = lv_obj_get_x(left_lbl) + lv_obj_get_width(left_lbl);
    static constexpr int SAFE_GAP = 4;
    int arrow_x = right_min_x - SAFE_GAP - vis;
    if (arrow_x < left_right_edge + 1) arrow_x = left_right_edge + 1;
    int arrow_y = row_y(ROW_CENTER) + (row_h() - vis) / 2;

    lv_obj_t *arrow = lv_img_create(parent);
    if (!arrow) return;
    lv_img_set_src(arrow, img_right_arrow_.c_str());
    lv_image_set_pivot(arrow, 0, 0);
    lv_image_set_scale(arrow, ARROW_SCALE);
    lv_obj_set_pos(arrow, arrow_x, arrow_y);
    lv_obj_move_to_index(arrow, 1);
}

void UISetupPage::place_fixed_sub_arrow(lv_obj_t *parent)
{
    static constexpr int ARROW_H = 19;
    lv_obj_t *arrow = lv_img_create(parent);
    if (!arrow) return;
    lv_img_set_src(arrow, img_right_arrow_.c_str());
    lv_obj_set_pos(arrow, SUB_RIGHT_ARROW_X, row_y(ROW_CENTER) + (row_h() - ARROW_H) / 2);
}

lv_obj_t *UISetupPage::create_menu_label(lv_obj_t *parent, int vi, int item_idx, int count)
{
    const char *text = (item_idx >= 0 && item_idx < count)
        ? menu_items_[item_idx].label.c_str() : "";
    lv_obj_t *lbl = create_carousel_label(parent, vi, ROW_CENTER, text, MENU_X);
    if (!lbl) return nullptr;
    if (item_idx < 0 || item_idx >= count) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    return lbl;
}

void UISetupPage::apply_overflow_handling(lv_obj_t *lbl, int box_left, int box_w, bool focused)
{
    if (!lbl || box_w <= 0) return;
    lv_obj_update_layout(lbl);
    if (lv_obj_get_width(lbl) <= box_w) return;
    lv_obj_set_width(lbl, box_w);
    lv_obj_set_x(lbl, box_left);
    lv_label_set_long_mode(lbl, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
}

void UISetupPage::apply_fixed_label_box(lv_obj_t *lbl, int x, int y, int w, bool scroll)
{
    if (!lbl || w <= 0) return;
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_width(lbl, w);
    lv_label_set_long_mode(lbl, scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
}

void UISetupPage::clamp_label_box(lv_obj_t *lbl, int x, int w, bool scroll)
{
    if (!lbl || w <= 0) return;
    lv_obj_set_x(lbl, x);
    lv_obj_set_width(lbl, w);
    lv_label_set_long_mode(lbl, scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
}

void UISetupPage::apply_overflow_centered(lv_obj_t *lbl, int center_x, int box_w, bool focused)
{
    apply_overflow_handling(lbl, center_x - box_w / 2, box_w, focused);
}
