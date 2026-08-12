/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "helloworld.hpp"

#include "input_keys.h"
#include "cp0_keyboard_navigation_contract.h"

#include <cstdint>

namespace {

struct Palette {
    lv_color_t background;
    lv_color_t surface;
    lv_color_t bar;
    lv_color_t button;
    lv_color_t border;
    lv_color_t text;
    lv_color_t muted_text;
};

Palette palette(bool dark_mode)
{
    if (dark_mode) {
        return {lv_color_hex(0x101214), lv_color_hex(0x15181C), lv_color_hex(0x181B1F),
                lv_color_hex(0x2A2F35), lv_color_hex(0x30363D), lv_color_hex(0xF4F4F5),
                lv_color_hex(0x6F7782)};
    }
    return {lv_color_hex(0xF8F9FA), lv_color_hex(0xFFFFFF), lv_color_hex(0xF0F0F0),
            lv_color_hex(0xE7E7E7), lv_color_hex(0xD0D7DE), lv_color_hex(0x1F2328),
            lv_color_hex(0xA8ADB3)};
}

void plain_container(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *centered_label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

} // namespace

UIIpPanelPage::UIIpPanelPage()
{
    set_page_title("Template App");
    create_ui();
    lv_obj_add_event_cb(root_screen_, key_event_cb, LV_EVENT_KEY, this);
    update_ui();
}

void UIIpPanelPage::create_ui()
{
    background_ = lv_obj_create(ui_APP_Container);
    plain_container(background_);
    lv_obj_set_size(background_, 320, 150);

    content_ = lv_obj_create(background_);
    plain_container(content_);
    lv_obj_set_size(content_, 320, 120);
    lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, 0);

    navigation_ = lv_obj_create(background_);
    plain_container(navigation_);
    lv_obj_set_size(navigation_, 320, 30);
    lv_obj_align(navigation_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(navigation_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(navigation_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(navigation_, 8, 0);

    for (int index = 0; index < 5; ++index) {
        buttons_[index] = lv_button_create(navigation_);
        lv_obj_set_size(buttons_[index], 40, 24);
        lv_obj_set_style_radius(buttons_[index], 3, 0);
        lv_obj_set_style_shadow_width(buttons_[index], 0, 0);
        lv_obj_set_style_pad_all(buttons_[index], 0, 0);
        lv_obj_add_event_cb(buttons_[index], button_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(buttons_[index], reinterpret_cast<void *>(static_cast<intptr_t>(index)));
        button_labels_[index] = centered_label(buttons_[index], "", &lv_font_montserrat_20);
        lv_obj_center(button_labels_[index]);
    }

    hello_group_ = lv_obj_create(content_);
    plain_container(hello_group_);
    lv_obj_set_size(hello_group_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hello_group_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hello_group_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hello_group_, 8, 0);
    lv_obj_center(hello_group_);

    greeting_label_ = centered_label(hello_group_, "Hello World", &lv_font_montserrat_20);
    info_label_ = centered_label(hello_group_, "", &lv_font_montserrat_12);
    lv_label_set_text_fmt(info_label_, "LVGL v%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
                          LVGL_VERSION_PATCH);
    counter_label_ = centered_label(content_, "Counter: 0", &lv_font_montserrat_20);
    lv_obj_center(counter_label_);
}

void UIIpPanelPage::update_ui()
{
    const Palette colors = palette(dark_mode_);
    lv_obj_set_style_bg_color(background_, colors.background, 0);
    lv_obj_set_style_bg_opa(background_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(content_, colors.surface, 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(navigation_, colors.bar, 0);
    lv_obj_set_style_bg_opa(navigation_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(greeting_label_, colors.text, 0);
    lv_obj_set_style_text_font(greeting_label_, bold_text_ ? &lv_font_montserrat_22 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(info_label_, colors.text, 0);
    lv_obj_set_style_text_color(counter_label_, colors.text, 0);
    lv_label_set_text_fmt(counter_label_, "Counter: %d", counter_);

    if (info_visible_) lv_obj_remove_flag(info_label_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(info_label_, LV_OBJ_FLAG_HIDDEN);
    if (counter_page_) {
        lv_obj_add_flag(hello_group_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(counter_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(hello_group_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(counter_label_, LV_OBJ_FLAG_HIDDEN);
    }

    const char *hello_icons[] = {LV_SYMBOL_POWER, "B", LV_SYMBOL_TINT, "i", LV_SYMBOL_RIGHT};
    const char *counter_icons[] = {LV_SYMBOL_POWER, LV_SYMBOL_MINUS, LV_SYMBOL_TINT, LV_SYMBOL_PLUS, LV_SYMBOL_LEFT};
    const char **icons = counter_page_ ? counter_icons : hello_icons;
    for (int index = 0; index < 5; ++index) {
        lv_label_set_text(button_labels_[index], icons[index]);
        lv_obj_set_style_text_color(button_labels_[index], colors.text, 0);
        lv_obj_set_style_bg_color(buttons_[index], index == selected_button_ ? colors.button : colors.bar, 0);
        lv_obj_set_style_bg_opa(buttons_[index], index == selected_button_ ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(buttons_[index], index == selected_button_ ? 1 : 0, 0);
        lv_obj_set_style_border_color(buttons_[index], colors.border, 0);
    }
}

void UIIpPanelPage::activate(int index)
{
    if (index == 0) info_visible_ = true;
    else if (index == 1) counter_page_ ? counter_ = counter_ > 0 ? counter_ - 1 : 0 : bold_text_ = !bold_text_;
    else if (index == 2) dark_mode_ = !dark_mode_;
    else if (index == 3) counter_page_ ? ++counter_ : info_visible_ = !info_visible_;
    else if (index == 4) counter_page_ = !counter_page_;
    update_ui();
}

void UIIpPanelPage::handle_key(lv_event_t *event)
{
    uint32_t key = lv_event_get_key(event);
    key = cp0_keyboard_navigation_alias(key);
    if (key == LV_KEY_LEFT || key == KEY_LEFT) selected_button_ = (selected_button_ + 4) % 5;
    else if (key == LV_KEY_RIGHT || key == KEY_RIGHT) selected_button_ = (selected_button_ + 1) % 5;
    else if (key == LV_KEY_ENTER) activate(selected_button_);
    else if (key == LV_KEY_ESC && navigate_home) navigate_home();
    update_ui();
}

void UIIpPanelPage::button_event_cb(lv_event_t *event)
{
    auto *self = static_cast<UIIpPanelPage *>(lv_event_get_user_data(event));
    self->activate(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)))));
}

void UIIpPanelPage::key_event_cb(lv_event_t *event)
{
    static_cast<UIIpPanelPage *>(lv_event_get_user_data(event))->handle_key(event);
}
