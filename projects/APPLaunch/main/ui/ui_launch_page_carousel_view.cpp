/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "animation/launcher_carousel_layout.h"
#include "launcher_platform.hpp"
#include "sample_log.h"

#include <algorithm>
#include <string>

namespace {

using launcher_carousel_layout::Slot;
using launcher_carousel_layout::kSlots;

static_assert(UILaunchPage::kPageDot0 == launcher_carousel_layout::kElementCount);
static_assert(UILaunchPage::kLauncherCarouselElementCount ==
              launcher_carousel_layout::kElementCount + launcher_carousel_layout::kPanelCount);

lv_obj_t *create_page_dot(lv_obj_t *parent, lv_coord_t x, bool selected)
{
    lv_obj_t *dot = lv_obj_create(parent);
    if (!dot) return nullptr;
    const lv_coord_t size = selected ? 10 : 5;
    const lv_color_t color = lv_color_hex(selected ? 0xCCCC33 : 0x4A4C4A);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_pos(dot, x, 70);
    lv_obj_set_align(dot, LV_ALIGN_CENTER);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(dot, lv_color_hex(0x4A4C4A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(dot, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    return dot;
}

lv_obj_t *create_carousel_card(lv_obj_t *parent, const Slot &slot,
                               lv_coord_t radius, uint32_t border_color,
                               bool clickable)
{
    lv_obj_t *card = lv_obj_create(parent);
    if (!card) return nullptr;
    lv_obj_set_size(card, slot.width, slot.height);
    lv_obj_set_pos(card, slot.x, slot.y);
    lv_obj_set_align(card, LV_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (!clickable)
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    if (slot.hidden)
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(card, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(border_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_post(card, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    return card;
}

lv_obj_t *create_carousel_title(lv_obj_t *parent, const Slot &slot,
                                lv_coord_t width, const char *text)
{
    lv_obj_t *title = lv_label_create(parent);
    if (!title) return nullptr;
    lv_obj_set_size(title, width, LV_SIZE_CONTENT);
    lv_obj_set_pos(title, slot.x, slot.y);
    lv_obj_set_align(title, LV_ALIGN_CENTER);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(title, text);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(
        title,
        launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(title, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (slot.hidden)
        lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);
    return title;
}

lv_obj_t *create_carousel_arrow(lv_obj_t *parent, lv_coord_t x, const char *asset)
{
    lv_obj_t *button = lv_btn_create(parent);
    if (!button) return nullptr;
    lv_obj_set_size(button, 17, 23);
    lv_obj_set_pos(button, x, -4);
    lv_obj_set_align(button, LV_ALIGN_CENTER);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(button, launcher_platform::path_c(asset), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    return button;
}

} // namespace

void UILaunchPage::set_page_dot_selected(size_t element, bool selected)
{
    if (element >= carousel_elements_.size() || !carousel_elements_[element]) return;
    lv_obj_t *dot = carousel_elements_[element];
    const lv_coord_t size = selected ? 10 : 5;
    const lv_color_t color = lv_color_hex(selected ? 0xCCCC33 : 0x4A4C4A);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_align(dot, LV_ALIGN_CENTER);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(dot, lv_color_hex(0x4A4C4A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(dot, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UILaunchPage::set_carousel_element_clickable(size_t element, bool clickable)
{
    if (element >= carousel_elements_.size() || !carousel_elements_[element]) return;
    if (clickable)
        lv_obj_add_flag(carousel_elements_[element], LV_OBJ_FLAG_CLICKABLE);
    else
        lv_obj_clear_flag(carousel_elements_[element], LV_OBJ_FLAG_CLICKABLE);
}

void UILaunchPage::set_panel_icon(lv_obj_t *panel, const char *src)
{
    if (!panel)
        return;

    const char *icon_src = src ? src : "";
    if (icon_src[0] == '\0') {
        SLOGW("[LAUNCHER] set panel icon with empty path");
    } else {
        bool exists = false;
        cp0_signal_filesystem_api({"Exists", icon_src}, [&](int code, std::string data) {
            exists = code == 0 && data == "1";
        });
        if (!exists)
            SLOGW("[LAUNCHER] set panel icon missing/unreadable: %s", icon_src);
    }

    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *image = lv_obj_get_child(panel, 0);
    if (!image || !lv_obj_check_type(image, &lv_image_class)) {
        image = lv_image_create(panel);
        if (!image) return;
        lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
        lv_obj_set_align(image, LV_ALIGN_CENTER);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
    }
    lv_image_set_src(image, icon_src);
}

void UILaunchPage::update_carousel_slot(size_t slot, const char *title, const char *icon)
{
    update_carousel_item(panel(slot), label(slot), title, icon);
}

void UILaunchPage::update_carousel_item(lv_obj_t *panel, lv_obj_t *label,
                                        const char *title, const char *icon)
{
    if (label)
        lv_label_set_text(label, title ? title : "");
    set_panel_icon(panel, icon);
}

void UILaunchPage::create_screen()
{
    const bool complete = carousel_container_ && left_arrow_button_ && right_arrow_button_ &&
        std::all_of(carousel_elements_.begin(), carousel_elements_.end(),
                    [](lv_obj_t *element) { return element != nullptr; });
    if (!complete)
        create_app_container(content_container());
}

void UILaunchPage::create_app_container(lv_obj_t *parent)
{
    if (!parent)
        return;

    lv_obj_t *container = lv_obj_create(parent);
    if (!container) return;
    lv_obj_set_size(container, 320, 150);
    lv_obj_set_align(container, LV_ALIGN_CENTER);
    lv_obj_clear_flag(container, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

    std::array<lv_obj_t *, kLauncherCarouselElementCount> elements = {};

    elements[kPageDot0] = create_page_dot(container, -20, navigation_.selected_page() == 0);
    elements[kPageDot1] = create_page_dot(container, -10, navigation_.selected_page() == 1);
    elements[kPageDot2] = create_page_dot(container, 0, navigation_.selected_page() == 2);
    elements[kPageDot3] = create_page_dot(container, 10, navigation_.selected_page() == 3);
    elements[kPageDot4] = create_page_dot(container, 20, navigation_.selected_page() == 4);

    elements[kTitleCenter] = create_carousel_title(container, kSlots[kTitleCenter], 100, "CLI");
    elements[kTitleRight] = create_carousel_title(container, kSlots[kTitleRight], 80, "GAME");
    elements[kTitleLeft] = create_carousel_title(container, kSlots[kTitleLeft], 80, "STORE");

    elements[kCardLeft] = create_carousel_card(container, kSlots[kCardLeft], 17, 0x222222, false);
    elements[kCardCenter] = create_carousel_card(container, kSlots[kCardCenter], 22, 0x444444, true);
    elements[kCardRight] = create_carousel_card(container, kSlots[kCardRight], 17, 0x222222, false);
    elements[kCardFarRight] = create_carousel_card(container, kSlots[kCardFarRight], 17, 0x333333, false);
    elements[kCardFarLeft] = create_carousel_card(container, kSlots[kCardFarLeft], 17, 0x333333, false);

    lv_obj_t *left_arrow = create_carousel_arrow(container, -151, "carousel_left_arrow.png");
    lv_obj_t *right_arrow = create_carousel_arrow(container, 150, "carousel_right_arrow.png");

    elements[kTitleFarLeft] = create_carousel_title(container, kSlots[kTitleFarLeft], 61, "one");
    elements[kTitleFarRight] = create_carousel_title(container, kSlots[kTitleFarRight], 61, "three");

    const bool complete = left_arrow && right_arrow &&
        std::all_of(elements.begin(), elements.end(), [](lv_obj_t *element) { return element != nullptr; });
    if (!complete) {
        lv_obj_delete(container);
        return;
    }
    *carousel_alive_ = false;
    if (carousel_container_) lv_obj_delete(carousel_container_);
    carousel_alive_ = std::make_shared<bool>(true);
    carousel_container_ = container;
    carousel_elements_ = elements;
    left_arrow_button_ = left_arrow;
    right_arrow_button_ = right_arrow;

    lv_obj_add_event_cb(carousel_elements_[kCardLeft], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_elements_[kCardCenter], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_elements_[kCardRight], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_elements_[kCardFarRight], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_elements_[kCardFarLeft], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(left_arrow_button_, on_left_arrow_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(right_arrow_button_, on_right_arrow_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_container_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    for (lv_obj_t *element : carousel_elements_)
        lv_obj_add_event_cb(element, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(left_arrow_button_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(right_arrow_button_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    if (!home_key_registered_ && screen()) {
        lv_obj_add_event_cb(screen(), on_home_key, (lv_event_code_t)LV_EVENT_KEYBOARD, this);
        home_key_registered_ = true;
    }
}
