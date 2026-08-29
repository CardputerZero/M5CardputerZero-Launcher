/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "launch.h"

#include "animation/ui_launcher_animation.h"
#include "animation/launcher_carousel_layout.h"

#include <algorithm>

void UILaunchPage::rotate_carousel_left(size_t start, size_t end)
{
    auto &items = carousel_elements_;
    std::rotate(items.begin() + start, items.begin() + start + 1, items.begin() + end + 1);
}

void UILaunchPage::rotate_carousel_right(size_t start, size_t end)
{
    auto &items = carousel_elements_;
    std::rotate(items.begin() + start, items.begin() + end, items.begin() + end + 1);
}

namespace {

UILaunchPage *active_launch_page = nullptr;
lv_group_t *home_input_group = nullptr;
using launcher_carousel_layout::Slot;
using launcher_carousel_layout::kSlots;
static_assert(UILaunchPage::kPageDot0 == launcher_carousel_layout::kElementCount);
static_assert(UILaunchPage::kLauncherCarouselElementCount ==
              launcher_carousel_layout::kElementCount + launcher_carousel_layout::kPanelCount);
static_assert(LauncherNavigationModel::PAGE_COUNT ==
              UILaunchPage::kPageDot4 - UILaunchPage::kPageDot0 + 1);


// ============================================================
// Force the panel to the specified slot
// ============================================================

static void snap_panel_to_slot(lv_obj_t *panel, int slot)
{
    const Slot &layout = kSlots[slot];
    lv_obj_set_x(panel, layout.x);
    lv_obj_set_y(panel, layout.y);
    lv_obj_set_width(panel, layout.width);
    lv_obj_set_height(panel, layout.height);

    if (layout.hidden)
    {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}


// ============================================================
// Force the label to the specified slot
// ============================================================

static void snap_label_to_slot(lv_obj_t *label, int slot)
{
    const Slot &layout = kSlots[slot];
    lv_obj_set_x(label, layout.x);
    lv_obj_set_y(label, layout.y);

    // Constrain label width to the matching card width so long names never overflow.
    // Label slots are 5..9; corresponding card slots are 0..4.
    const int card_w = kSlots[slot - launcher_carousel_layout::kTitleOffset].width;
    lv_obj_set_width(label, card_w > 0 ? card_w : LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (layout.hidden)
    {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}


} // namespace


lv_group_t *UILaunchPage::home_input_group()
{
    if (active_launch_page)
        return active_launch_page->input_group();
    return ::home_input_group;
}

lv_obj_t *UILaunchPage::panel(size_t slot)
{
    return carousel_elements_[kCardFarLeft + slot];
}

lv_obj_t *UILaunchPage::label(size_t slot)
{
    return carousel_elements_[kTitleFarLeft + slot];
}

void UILaunchPage::bind_home_input_group()
{
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (indev) {
        lv_indev_set_group(indev, home_input_group());
    }
}

void UILaunchPage::init_input_group()
{
    ::home_input_group = input_group();
    bind_home_input_group();
}

UILaunchPage::UILaunchPage(Launch *launch)
    : home_base(), launch_(launch)
{
    active_launch_page = this;
    if (root_screen_)
        lv_obj_add_event_cb(root_screen_, UILaunchPage::on_root_deleted,
                            LV_EVENT_DELETE, this);
}

UILaunchPage::~UILaunchPage()
{
    startup_sound_state_->alive.store(false);
    *carousel_alive_ = false;
    navigation_.cancel_navigation();
    if (carousel_container_) {
        lv_obj_remove_event_cb_with_user_data(
            carousel_container_, UILaunchPage::on_owned_obj_deleted, this);
        for (lv_obj_t *element : carousel_elements_) {
            if (element)
                lv_obj_remove_event_cb_with_user_data(
                    element, UILaunchPage::on_owned_obj_deleted, this);
        }
        if (left_arrow_button_)
            lv_obj_remove_event_cb_with_user_data(
                left_arrow_button_, UILaunchPage::on_owned_obj_deleted, this);
        if (right_arrow_button_)
            lv_obj_remove_event_cb_with_user_data(
                right_arrow_button_, UILaunchPage::on_owned_obj_deleted, this);
        lv_obj_delete(carousel_container_);
        carousel_container_ = nullptr;
        carousel_elements_.fill(nullptr);
        left_arrow_button_ = nullptr;
        right_arrow_button_ = nullptr;
    }
    if (home_key_registered_ && root_screen_) {
        lv_obj_remove_event_cb_with_user_data(
            root_screen_, UILaunchPage::on_home_key, this);
        home_key_registered_ = false;
    }
    if (root_screen_)
        lv_obj_remove_event_cb_with_user_data(
            root_screen_, UILaunchPage::on_root_deleted, this);
    stop_startup_delay();
    stop_startup_sound_timer();
    release_startup_screen(startup_logo_scr_);
    release_startup_screen(startup_gif_);
    if (active_launch_page == this)
        active_launch_page = nullptr;
    if (::home_input_group == input_group())
        ::home_input_group = nullptr;
}

void UILaunchPage::release_startup_screen(lv_obj_t *&screen)
{
    if (!screen) return;

    lv_display_t *display = lv_display_get_default();
    auto display_references = [display](lv_obj_t *candidate) {
        return display && candidate &&
            (lv_display_get_screen_active(display) == candidate ||
             lv_display_get_screen_loading(display) == candidate ||
             lv_display_get_screen_prev(display) == candidate);
    };
    auto detach_from_page = [this](lv_obj_t *object) {
        lv_obj_remove_event_cb_with_user_data(
            object, UILaunchPage::on_owned_obj_deleted, this);
        lv_obj_remove_event_cb_with_user_data(
            object, UILaunchPage::on_startup_gif_event, this);
    };

    const bool display_references_screen = display_references(screen);
    if (display_references_screen) {
        if (root_screen_ && lv_display_get_screen_active(display) != root_screen_) {
            lv_screen_load(root_screen_);
        } else {
            detach_from_page(screen);
            screen = nullptr;
            return;
        }
        if (display_references(screen)) {
            detach_from_page(screen);
            screen = nullptr;
            return;
        }
    }

    if (screen) lv_obj_delete(screen);
    screen = nullptr;
}

void UILaunchPage::on_root_deleted(lv_event_t *event) noexcept
{
    try {
        auto *self = static_cast<UILaunchPage *>(lv_event_get_user_data(event));
        if (!self || lv_event_get_target(event) != lv_event_get_current_target(event) ||
            lv_event_get_target(event) != self->root_screen_)
            return;
        self->stop_startup_delay();
        self->stop_startup_sound_timer();
        self->startup_sound_state_->alive.store(false);
        *self->carousel_alive_ = false;
        self->carousel_elements_.fill(nullptr);
        self->carousel_container_ = nullptr;
        self->left_arrow_button_ = nullptr;
        self->right_arrow_button_ = nullptr;
        self->home_key_registered_ = false;
        self->navigation_.cancel_navigation();
    } catch (...) {
    }
}

void UILaunchPage::on_owned_obj_deleted(lv_event_t *event) noexcept
{
    try {
        auto *self = static_cast<UILaunchPage *>(lv_event_get_user_data(event));
        lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (!self || !target || target != lv_event_get_current_target(event)) return;
        const bool recover_gif = launcher_startup_should_recover_deleted_gif(
            self->startup_gif_ == target, self->startup_gif_done_);
        if (self->startup_logo_scr_ == target) self->startup_logo_scr_ = nullptr;
        if (self->startup_gif_ == target) self->startup_gif_ = nullptr;
        bool carousel_object_deleted = self->carousel_container_ == target ||
            self->left_arrow_button_ == target || self->right_arrow_button_ == target;
        for (lv_obj_t *&element : self->carousel_elements_) {
            if (element == target) {
                element = nullptr;
                carousel_object_deleted = true;
            }
        }
        if (self->left_arrow_button_ == target) self->left_arrow_button_ = nullptr;
        if (self->right_arrow_button_ == target) self->right_arrow_button_ = nullptr;
        if (self->carousel_container_ == target) {
            self->carousel_container_ = nullptr;
            self->carousel_elements_.fill(nullptr);
            self->left_arrow_button_ = nullptr;
            self->right_arrow_button_ = nullptr;
        }
        if (carousel_object_deleted) {
            *self->carousel_alive_ = false;
            self->cancel_switch_animation();
        }
        if (recover_gif) {
            self->startup_gif_done_ = true;
            self->load_home_screen();
        }
    } catch (...) {
    }
}

void UILaunchPage::fill_right_entering_slot(lv_obj_t *panel, lv_obj_t *label)
{
    if (!launch_)
        return;

    launch_->select_next_app();
    if (const app *item = launch_->carousel_slot_app(kCardFarRight))
        update_carousel_item(panel, label, item->Name.c_str(), item->Icon);
}

void UILaunchPage::fill_left_entering_slot(lv_obj_t *panel, lv_obj_t *label)
{
    if (!launch_)
        return;

    launch_->select_previous_app();
    if (const app *item = launch_->carousel_slot_app(kCardFarLeft))
        update_carousel_item(panel, label, item->Name.c_str(), item->Icon);
}

void UILaunchPage::refresh_carousel()
{
    if (!launch_)
        return;

    for (size_t slot = 0; slot < launcher_carousel_layout::kPanelCount; ++slot) {
        if (const app *item = launch_->carousel_slot_app(slot))
            update_carousel_slot(slot, item->Name.c_str(), item->Icon);
    }
}

void UILaunchPage::reload_home_icons(const std::vector<std::string> &icon_paths)
{
    HomeIconBufferPool replacement;
    replacement.rebuild(icon_paths);
    home_icon_pool_.swap(replacement);

    // Keep the old pool alive until every image widget no longer references it.
    for (size_t slot = 0; slot < launcher_carousel_layout::kPanelCount; ++slot)
        set_panel_icon(panel(slot), {});
    refresh_carousel();
}

void UILaunchPage::launch_selected_app()
{
    if (launch_)
        launch_->launch_app();
}

void UILaunchPage::finish_switch_animation()
{
    if (std::any_of(carousel_elements_.begin(), carousel_elements_.begin() +
                    launcher_carousel_layout::kElementCount,
                    [](lv_obj_t *element) { return element == nullptr; })) {
        cancel_switch_animation();
        return;
    }
    for (size_t i = 0; i < launcher_carousel_layout::kPanelCount; ++i)
    {
        snap_panel_to_slot(carousel_elements_[i], i);
    }

    for (size_t i = launcher_carousel_layout::kTitleOffset;
         i < launcher_carousel_layout::kElementCount; ++i)
    {
        snap_label_to_slot(carousel_elements_[i], i);
    }

    navigation_.finish_navigation();

    for (size_t i = 0; i < launcher_carousel_layout::kPanelCount; ++i) {
        uint32_t color = (i == kCardCenter) ? BORDER_COLOR_CENTER : BORDER_COLOR_SIDE;
        lv_obj_set_style_border_color(carousel_elements_[i], lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    for (size_t i = launcher_carousel_layout::kTitleOffset;
         i < launcher_carousel_layout::kElementCount; ++i) {
        lv_obj_set_style_text_font(carousel_elements_[i], launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

}

void UILaunchPage::cancel_switch_animation()
{
    if (!navigation_.cancel_navigation()) return;
    if (launch_) {
        if (active_navigation_moves_next_)
            launch_->select_previous_app();
        else
            launch_->select_next_app();
    }
}

void UILaunchPage::switch_right()
{
    if (std::any_of(carousel_elements_.begin(), carousel_elements_.begin() +
                    launcher_carousel_layout::kElementCount,
                    [](lv_obj_t *element) { return element == nullptr; })) return;
    const size_t previous_page = navigation_.selected_page();
    if (!navigation_.begin_navigation(LauncherNavigationDirection::PREVIOUS))
        return;
    active_navigation_moves_next_ = false;

    lv_obj_clear_flag(carousel_elements_[0], LV_OBJ_FLAG_HIDDEN);

    std::weak_ptr<bool> alive = carousel_alive_;
    launcher_home_animation::animate_right(
        carousel_elements_.data(),
        [this, alive]() {
            auto state = alive.lock();
            if (state && *state) finish_switch_animation();
        },
        [alive]() {
            auto state = alive.lock();
            return state && *state;
        });

    snap_panel_to_slot(carousel_elements_[4], 0);

    lv_obj_clear_flag(carousel_elements_[5], LV_OBJ_FLAG_HIDDEN);

    snap_label_to_slot(carousel_elements_[9], 5);

    fill_left_entering_slot(carousel_elements_[4], carousel_elements_[9]);

    set_carousel_element_clickable(kCardCenter, false);
    rotate_carousel_right(0, 4);
    set_carousel_element_clickable(kCardCenter, true);

    rotate_carousel_right(5, 9);

    set_page_dot_selected(kPageDot0 + previous_page, false);
    set_page_dot_selected(kPageDot0 + navigation_.selected_page(), true);
}

void UILaunchPage::switch_left()
{
    if (std::any_of(carousel_elements_.begin(), carousel_elements_.begin() +
                    launcher_carousel_layout::kElementCount,
                    [](lv_obj_t *element) { return element == nullptr; })) return;
    const size_t previous_page = navigation_.selected_page();
    if (!navigation_.begin_navigation(LauncherNavigationDirection::NEXT))
        return;
    active_navigation_moves_next_ = true;

    lv_obj_clear_flag(carousel_elements_[4], LV_OBJ_FLAG_HIDDEN);

    std::weak_ptr<bool> alive = carousel_alive_;
    launcher_home_animation::animate_left(
        carousel_elements_.data(),
        [this, alive]() {
            auto state = alive.lock();
            if (state && *state) finish_switch_animation();
        },
        [alive]() {
            auto state = alive.lock();
            return state && *state;
        });

    snap_panel_to_slot(carousel_elements_[0], 4);

    lv_obj_clear_flag(carousel_elements_[9], LV_OBJ_FLAG_HIDDEN);

    snap_label_to_slot(carousel_elements_[5], 9);

    fill_right_entering_slot(carousel_elements_[0], carousel_elements_[5]);

    set_carousel_element_clickable(kCardCenter, false);
    rotate_carousel_left(0, 4);
    set_carousel_element_clickable(kCardCenter, true);

    rotate_carousel_left(5, 9);

    set_page_dot_selected(kPageDot0 + previous_page, false);
    set_page_dot_selected(kPageDot0 + navigation_.selected_page(), true);
}
