/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "animation/launcher_carousel_layout.h"
#include "launcher_platform.hpp"

#include <algorithm>
#include <string>

namespace {

using launcher_carousel_layout::Slot;
using launcher_carousel_layout::kSlots;

// 320 px is the display width; use its golden-section width for the slider.
constexpr lv_coord_t kHomeSliderWidth = 198;
constexpr lv_coord_t kHomeSliderHeight = 6;

static_assert(UILaunchPage::kPageSlider == launcher_carousel_layout::kElementCount);
static_assert(UILaunchPage::kLauncherCarouselElementCount ==
              launcher_carousel_layout::kElementCount + 1);

lv_obj_t *create_page_slider(lv_obj_t *parent, size_t page_count, size_t selected_page)
{
    lv_obj_t *slider = lv_slider_create(parent);
    if (!slider) return nullptr;

    // Use the centered golden-section width while keeping the track slim.
    lv_obj_remove_style_all(slider);
    lv_obj_set_size(slider, kHomeSliderWidth, kHomeSliderHeight);
    lv_obj_set_pos(slider, 0, 70);
    lv_obj_set_align(slider, LV_ALIGN_CENTER);
    lv_obj_clear_flag(
        slider, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                           LV_OBJ_FLAG_CLICK_FOCUSABLE |
                                           LV_OBJ_FLAG_SCROLLABLE |
                                           LV_OBJ_FLAG_SCROLL_ON_FOCUS));

    lv_obj_set_style_bg_color(slider, lv_color_hex(0x4A4C4A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);
    // Keep the knob fully visible at both ends of the golden-section track.
    lv_obj_set_style_pad_left(slider, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_right(slider, 5, LV_PART_MAIN);
    // The indicator is intentionally the same color as the track: the
    // yellow knob alone marks the current icon position.
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x4A4C4A), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xCCCC33), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 2, LV_PART_KNOB);
    const size_t max_page = page_count > 0 ? page_count - 1 : 0;
    lv_slider_set_range(slider, 0, static_cast<int32_t>(max_page));
    lv_slider_set_value(slider, static_cast<int32_t>(selected_page), LV_ANIM_OFF);
    return slider;
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

void UILaunchPage::set_page_slider_range(size_t page_count, size_t selected_page)
{
    lv_obj_t *slider = carousel_elements_[kPageSlider];
    if (!slider) return;
    const size_t max_page = page_count > 0 ? page_count - 1 : 0;
    lv_slider_set_range(slider, 0, static_cast<int32_t>(max_page));
    set_page_slider_position(selected_page);
}

void UILaunchPage::set_page_slider_position(size_t page)
{
    lv_obj_t *slider = carousel_elements_[kPageSlider];
    if (!slider) return;
    const int32_t max_page = lv_slider_get_max_value(slider);
    const int32_t value = page > static_cast<size_t>(max_page)
        ? max_page : static_cast<int32_t>(page);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
}

void UILaunchPage::set_carousel_element_clickable(size_t element, bool clickable)
{
    if (element >= carousel_elements_.size() || !carousel_elements_[element]) return;
    if (clickable)
        lv_obj_add_flag(carousel_elements_[element], LV_OBJ_FLAG_CLICKABLE);
    else
        lv_obj_clear_flag(carousel_elements_[element], LV_OBJ_FLAG_CLICKABLE);
}

void UILaunchPage::set_panel_icon(lv_obj_t *panel, const std::string &src)
{
    if (!panel)
        return;

    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *clip = lv_obj_get_child(panel, 0);
    lv_obj_t *image = clip ? lv_obj_get_child(clip, 0) : nullptr;
    if (!clip || !image || !lv_obj_check_type(image, &lv_image_class)) {
        clip = lv_obj_create(panel);
        if (!clip) return;
        lv_obj_set_size(clip, LV_PCT(100), LV_PCT(100));
        lv_obj_set_align(clip, LV_ALIGN_CENTER);
        lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
        image = lv_image_create(clip);
        if (!image) return;
        lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
        lv_obj_set_align(image, LV_ALIGN_CENTER);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
    }

    // Clip the image in a child container. LVGL applies clip_corner to an
    // object's children, not to the object's own draw operation.
    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    const lv_coord_t card_radius = lv_obj_get_style_radius(panel, LV_PART_MAIN);
    const lv_coord_t card_border = lv_obj_get_style_border_width(panel, LV_PART_MAIN);
    lv_obj_set_style_radius(clip, std::max<lv_coord_t>(0, card_radius - card_border), selector);
    lv_obj_set_style_clip_corner(clip, true, selector);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, selector);
    lv_obj_set_style_border_width(clip, 0, selector);
    lv_obj_set_style_pad_all(clip, 0, selector);

    // Match the decoded buffer to the card content size. The far slots are
    // hidden while they enter the carousel, so use the side-card size for
    // them and avoid a second transparent-edge resample when they appear.
    const lv_coord_t source_card_size = lv_obj_get_width(panel) >= 100 ? 100 : 80;
    const uint32_t image_size = static_cast<uint32_t>(std::max<lv_coord_t>(
        1, source_card_size - 2 * card_border));
    lv_image_set_src(image, home_icon_pool_.find(src, image_size));
}

void UILaunchPage::update_carousel_slot(size_t slot, const char *title, const std::string &icon)
{
    update_carousel_item(panel(slot), label(slot), title, icon);
}

void UILaunchPage::update_carousel_item(lv_obj_t *panel, lv_obj_t *label,
                                        const char *title, const std::string &icon)
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

    const size_t page_count = launch_ ? launch_->app_count() : 0;
    const size_t selected_page = launch_ ? launch_->current_app_index() : 0;
    elements[kPageSlider] = create_page_slider(container, page_count, selected_page);

    elements[kTitleCenter] = create_carousel_title(container, kSlots[kTitleCenter], 100, "CLI");
    elements[kTitleRight] = create_carousel_title(container, kSlots[kTitleRight], 80, "GAME");
    elements[kTitleLeft] = create_carousel_title(container, kSlots[kTitleLeft], 80, "STORE");

    elements[kCardLeft] = create_carousel_card(
        container, kSlots[kCardLeft], launcher_carousel_layout::kSideCardRadius,
        0x222222, false);
    elements[kCardCenter] = create_carousel_card(
        container, kSlots[kCardCenter], launcher_carousel_layout::kCenterCardRadius,
        0x444444, true);
    elements[kCardRight] = create_carousel_card(
        container, kSlots[kCardRight], launcher_carousel_layout::kSideCardRadius,
        0x222222, false);
    elements[kCardFarRight] = create_carousel_card(
        container, kSlots[kCardFarRight], launcher_carousel_layout::kSideCardRadius,
        0x333333, false);
    elements[kCardFarLeft] = create_carousel_card(
        container, kSlots[kCardFarLeft], launcher_carousel_layout::kSideCardRadius,
        0x333333, false);

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
