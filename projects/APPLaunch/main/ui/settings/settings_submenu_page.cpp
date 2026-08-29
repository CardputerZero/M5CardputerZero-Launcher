/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_submenu_page.hpp"

#include <exception>
#include <utility>

LvSettingRollerPage2::LvSettingRollerPage2() = default;

void LvSettingRollerPage2::AnimateNextIn(std::function<void()> animate_over_func)
{
    if (roller3_ && !page3_transitioning_) {
        page3_animation_over_ = std::move(animate_over_func);
        start_page3_transition(false);
    } else if (animate_over_func) {
        animate_over_func();
    }
}

void LvSettingRollerPage2::AnimateNextOut(std::function<void()> animate_over_func)
{
    if (roller3_ && !page3_transitioning_) {
        page3_animation_over_ = std::move(animate_over_func);
        start_page3_transition(true);
    } else if (animate_over_func) {
        animate_over_func();
    }
}

void LvSettingRollerPage2::LoadNextPage()
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

void LvSettingRollerPage2::LeaveNextPage()
{
    back_from_third_page();
}

LvSettingRollerPage2::~LvSettingRollerPage2()
{
    stop_direct_status_poll();
    cancel_async_tasks();
    if (power_warning_) {
        if (lv_group_t *group = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr)
            lv_group_remove_obj(power_warning_);
        lv_msgbox_close(power_warning_);
        power_warning_ = nullptr;
    }
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

LvSettingRollerPage2::LvSettingRollerPage2(lv_obj_t *parent, const NodeIter &parent_node)
    : parent_(parent), ui_APP_Container(parent), parent_node_(parent_node)
{
    create_ui(parent);
}

LvSettingRollerPage2::LvSettingRollerPage2(lv_obj_t *parent, const NodeIter &parent_node,
                                           std::function<void()> back_callback)
    : parent_(parent), ui_APP_Container(parent), parent_node_(parent_node)
{
    LeaveSelfPage = std::move(back_callback);
    create_ui(parent);
}

void LvSettingRollerPage2::style_label(lv_obj_t *label, int distance, bool compact)
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

    lv_obj_set_style_text_font(label, settings_fonts::sans(font_size, LV_FREETYPE_FONT_STYLE_BOLD),
                               LV_PART_MAIN);
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

void LvSettingRollerPage2::set_status_hint(const char *text, uint32_t color)
{
    if (!hint_) return;
    lv_label_set_text(hint_, text ? text : "");
    lv_obj_set_style_text_color(hint_, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_update_layout(hint_);
    lv_obj_set_pos(hint_, metric(LayoutMetric::PanelX) + metric(LayoutMetric::PanelW) - 6 - lv_obj_get_width(hint_),
                   metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(hint_)) / 2);
    hint_base_x_ = lv_obj_get_x(hint_);
}

void LvSettingRollerPage2::SetSelfUiMode(PageType mode)
{
    if (mode == PageType::FullCustom) return;

    const bool enabled = mode == PageType::NextPageNeeded;
    NextActive         = enabled;

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

std::string LvSettingRollerPage2::selected_entry_label() const
{
    if (selected_index < 0 || selected_index >= static_cast<int32_t>(item_count_)) return {};
    return std::next(parent_node_.begin(), selected_index)->label;
}

void LvSettingRollerPage2::show_power_warning()
{
    if (power_warning_ || !parent_) return;
    power_warning_ = lv_msgbox_create(parent_);
    if (!power_warning_) return;
    lv_obj_set_size(power_warning_, 280, 92);
    lv_obj_center(power_warning_);
    lv_obj_set_style_radius(power_warning_, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(power_warning_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(power_warning_, lv_color_hex(0xFFAA00), LV_PART_MAIN);
    lv_obj_set_style_bg_color(power_warning_, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(power_warning_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(power_warning_, 0, LV_PART_MAIN);
    lv_obj_clear_flag(power_warning_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title     = lv_msgbox_add_title(power_warning_, "Bluetooth power is off");
    lv_obj_t *header    = lv_msgbox_get_header(power_warning_);
    lv_obj_t *content   = lv_msgbox_get_content(power_warning_);
    lv_obj_t *message   = lv_msgbox_add_text(power_warning_, "Turn on Power before continuing.");
    lv_obj_t *ok_button = lv_msgbox_add_footer_button(power_warning_, "OK");
    lv_obj_t *footer    = lv_msgbox_get_footer(power_warning_);
    lv_obj_t *ok_label  = ok_button ? lv_obj_get_child(ok_button, 0) : nullptr;

    if (header) {
        lv_obj_set_height(header, 30);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(header, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(header, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_top(header, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(header, 0, LV_PART_MAIN);
    }
    if (title) {
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFAA00), LV_PART_MAIN);
        lv_obj_set_style_text_font(title, settings_fonts::sans(14, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }
    if (content) {
        lv_obj_set_height(content, 32);
        lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(content, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(content, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_top(content, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(content, 0, LV_PART_MAIN);
        lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    if (message) {
        lv_obj_set_style_text_color(message, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(message, settings_fonts::sans(12), LV_PART_MAIN);
        lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }
    if (footer) {
        lv_obj_set_height(footer, 28);
        lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(footer, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(footer, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(footer, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_top(footer, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(footer, 0, LV_PART_MAIN);
        lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    if (ok_button) {
        lv_obj_set_width(ok_button, 28);
        lv_obj_set_height(ok_button, 22);
        lv_obj_set_style_bg_opa(ok_button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ok_button, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ok_button, 0, LV_PART_MAIN);
    }
    if (ok_label) {
        lv_obj_set_style_text_color(ok_label, lv_color_hex(0x58A6FF), LV_PART_MAIN);
        lv_obj_set_style_text_font(ok_label, settings_fonts::sans(12), LV_PART_MAIN);
        lv_obj_set_style_text_align(ok_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }

    lv_group_t *group = lv_obj_get_group(ComponensObj);
    if (group) {
        lv_group_add_obj(group, power_warning_);
        lv_group_focus_obj(power_warning_);
    }
    DComponens::lvgl_bind_event(
        power_warning_, LV_EVENT_KEY, nullptr,
        std::bind(&LvSettingRollerPage2::handle_power_warning_key, this, std::placeholders::_1));
}

void LvSettingRollerPage2::set_compact_mode(bool enabled)
{
    compact_mode_ = enabled;
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

void LvSettingRollerPage2::update_arrow_visibility()
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

void LvSettingRollerPage2::set_status_icon(lv_obj_t *icon_obj, bool enabled)
{
    if (!icon_obj) return;

    lv_img_set_src(icon_obj, enabled ? &setting_ok : &setting_cross);
    lv_obj_update_layout(icon_obj);
    lv_obj_set_pos(icon_obj, metric(LayoutMetric::StatusIconX) + (enabled ? 0 : 1),
                   (metric(LayoutMetric::RowH) - lv_obj_get_height(icon_obj)) / 2);
}

void LvSettingRollerPage2::set_status_error(lv_obj_t *icon_obj)
{
    set_status_icon(icon_obj, false);
}

void LvSettingRollerPage2::direct_status_timer_cb(lv_timer_t *timer)
{
    auto *self = timer ? static_cast<LvSettingRollerPage2 *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!self || !self->direct_status_poll_ || !self->direct_status_poll_())
        if (self) self->stop_direct_status_poll();
}

void LvSettingRollerPage2::stop_direct_status_poll()
{
    if (direct_status_timer_) {
        lv_timer_delete(direct_status_timer_);
        direct_status_timer_ = nullptr;
    }
    direct_status_poll_ = nullptr;
}

void LvSettingRollerPage2::request_status_refresh(lv_obj_t *row, const NodeIter &selected_node)
{
    if (!row || !selected_node->Componens_api) return;

    // Rows keep the label first and the optional status icon second. Keep the
    // row as the API boundary so future refreshes can update either control.
    lv_obj_t *label = lv_obj_get_child(row, 0);
    lv_obj_t *icon_obj = lv_obj_get_child(row, 1);
    if (!label || !icon_obj) return;

    // Render a deterministic state immediately. Async status reads can take a
    // few frames, but the row should never appear to be missing its icon.
    set_status_icon(icon_obj, false);

    if (selected_node->status_read_policy == SettingStatusReadPolicy::Direct) {
        stop_direct_status_poll();
        direct_status_poll_ = [this, icon_obj, selected_node] {
            std::atomic_bool operation_started{false};
            SettingApiReadFlagTimeStartData result = std::make_tuple(false, &operation_started);
            try {
                selected_node->Componens_api(SettingApiReadFlagTimeStart, &result);
            } catch (...) {
                set_status_error(icon_obj);
                set_status_hint("Err", 0xEB5F5F);
                return false;
            }

            const bool pending = operation_started.load(std::memory_order_acquire);
            set_status_icon(icon_obj, std::get<0>(result));
            set_status_hint(pending ? "Wait" : "ok:enter", pending ? 0xF0C850 : 0x00CC66);
            return pending;
        };

        if (direct_status_poll_() && !(direct_status_timer_ = lv_timer_create(direct_status_timer_cb, 100, this)))
            stop_direct_status_poll();
        return;
    }

    auto dot_count         = std::make_shared<uint8_t>(0);
    auto last_label_update = std::make_shared<AsyncTaskContext::Clock::time_point>(AsyncTaskContext::Clock::now());
    auto operation_started = std::make_shared<std::atomic_bool>(false);
    const std::uint64_t refresh_generation = selected_node->status_generation;

    StatusTaskCallbacks callbacks;
    callbacks.execute = [selected_node, operation_started]() -> StatusQueryResult {
        SettingApiReadFlagTimeStartData result = std::make_tuple(false, operation_started.get());
        selected_node->Componens_api(SettingApiReadFlagTimeStart, &result);
        return {true, std::get<0>(result)};
    };
    callbacks.on_start     = [this](AsyncTaskContext &) { set_status_hint("Sel.", 0xF0C850); };
    callbacks.on_submitted = [this, last_label_update](AsyncTaskContext &) {
        *last_label_update = AsyncTaskContext::Clock::now();
        set_status_hint("Chk.", 0xF0C850);
    };
    callbacks.on_wait = [this, dot_count, last_label_update](AsyncTaskContext &) {
        const auto now = AsyncTaskContext::Clock::now();
        if (now - *last_label_update < std::chrono::seconds(1)) return;

        *last_label_update = now;
        *dot_count         = static_cast<uint8_t>((*dot_count + 1) % 2);
        set_status_hint(*dot_count ? "Wait" : "Chk.", 0xF0C850);
    };
    callbacks.on_complete = [this, icon_obj, selected_node, refresh_generation](AsyncTaskContext &,
                                                                                const StatusQueryResult &result) {
        if (selected_node->status_generation != refresh_generation) return;
        if (!result.success) {
            std::printf("[LvSettingRollerPage2] status query failed\n");
            set_status_error(icon_obj);
            set_status_hint("Err", 0xEB5F5F);
            return;
        }

        set_status_icon(icon_obj, result.enabled);
        set_status_hint("ok:enter", 0x00CC66);
    };
    callbacks.on_exception = [this, icon_obj, selected_node, refresh_generation](AsyncTaskContext &,
                                                                                 std::exception_ptr) {
        if (selected_node->status_generation != refresh_generation) return;
        std::printf("[LvSettingRollerPage2] status query raised an exception\n");
        set_status_error(icon_obj);
        set_status_hint("Err", 0xEB5F5F);
    };
    callbacks.on_timeout = [this, icon_obj, selected_node, refresh_generation](AsyncTaskContext &) {
        if (selected_node->status_generation != refresh_generation) return;
        std::printf("[LvSettingRollerPage2] status query timed out\n");
        set_status_error(icon_obj);
        set_status_hint("Err", 0xEB5F5F);
    };
    callbacks.on_schedule_failed = [this, icon_obj, selected_node, refresh_generation](AsyncTaskContext &) {
        if (selected_node->status_generation != refresh_generation) return;
        std::printf("[LvSettingRollerPage2] status query could not be scheduled\n");
        set_status_error(icon_obj);
        set_status_hint("Err", 0xEB5F5F);
    };

    run_async_task(std::move(callbacks));
}

void LvSettingRollerPage2::handle_power_warning_key(lv_event_t *event)
{
    handle_key_event(event);
}

void LvSettingRollerPage2::close_power_warning()
{
    if (!power_warning_) return;
    lv_group_t *group = lv_obj_get_group(ComponensObj);
    if (group) lv_group_remove_obj(power_warning_);
    lv_msgbox_close(power_warning_);
    power_warning_ = nullptr;
    if (group) {
        lv_group_add_obj(group, ComponensObj);
        lv_group_focus_obj(ComponensObj);
    }
}

void LvSettingRollerPage2::create_third_page(const NodeIter &page_node)
{
    if (page3_transitioning_ || roller3_ || !page_node->page_factory || !parent_) return;

    lv_group_t *group = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr;
    if (ComponensObj && group) lv_group_remove_obj(ComponensObj);
    input_group_ = group;

    roller3_ = page_node->page_factory(parent_, page_node, std::bind(&LvSettingRollerPage2::LeaveNextPage, this));
    if (!roller3_ || !roller3_->Get()) {
        roller3_.reset();
        if (ComponensObj && group) {
            lv_group_add_obj(group, ComponensObj);
            lv_group_focus_obj(ComponensObj);
        }
        return;
    }

    lv_obj_set_style_translate_x(roller3_->Get(), metric(LayoutMetric::PageWidth), LV_PART_MAIN);
    AnimateNextOut(nullptr);
}

void LvSettingRollerPage2::back_from_third_page()
{
    if (!roller3_ || page3_transitioning_) return;
    AnimateNextIn(nullptr);
}

void LvSettingRollerPage2::stop_page3_animations()
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

int LvSettingRollerPage2::page_object_base_x(lv_obj_t *object) const
{
    if (object == selection_bg_ || object == ComponensObj) return metric(LayoutMetric::PanelX);
    if (object == arrow_up_) return arrow_up_base_x_;
    if (object == arrow_down_) return arrow_down_base_x_;
    if (object == right_arrow_) return right_arrow_base_x_;
    if (object == hint_) return hint_base_x_;
    return object ? lv_obj_get_x(object) : 0;
}

void LvSettingRollerPage2::animate_object_to(lv_obj_t *object, int end_x)
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

void LvSettingRollerPage2::animate_page3_root(bool entering)
{
    if (!roller3_ || !roller3_->Get()) return;

    lv_obj_t *root = roller3_->Get();
    lv_anim_del(root, nullptr);
    const int start_x = entering ? metric(LayoutMetric::PageWidth) : metric(LayoutMetric::Page3X);
    const int end_x   = entering ? metric(LayoutMetric::Page3X) : metric(LayoutMetric::PageWidth);
    lv_obj_set_x(root, start_x);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, root);
    lv_anim_set_exec_cb(&animation, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_x));
    lv_anim_set_values(&animation, start_x, end_x);
    lv_anim_set_time(&animation, metric(LayoutMetric::Page3AnimMs));
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_user_data(&animation, this);
    lv_anim_set_completed_cb(
        &animation, entering ? &LvSettingRollerPage2::page3_enter_done_cb : &LvSettingRollerPage2::page3_leave_done_cb);
    if (!lv_anim_start(&animation)) {
        lv_obj_set_x(root, end_x);
        finish_page3_transition(entering);
    }
}

void LvSettingRollerPage2::animate_first_page_objects(bool entering)
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

int LvSettingRollerPage2::page2_target_x(lv_obj_t *object, bool entering) const
{
    const int base_x = page_object_base_x(object);
    if (object == selection_bg_ || object == ComponensObj) {
        return entering ? 0 : base_x;
    }
    return entering ? base_x - metric(LayoutMetric::PanelX) : base_x;
}

void LvSettingRollerPage2::start_page3_transition(bool entering)
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

void LvSettingRollerPage2::page3_enter_done_cb(lv_anim_t *animation)
{
    auto *self = static_cast<LvSettingRollerPage2 *>(lv_anim_get_user_data(animation));
    if (self) self->finish_page3_transition(true);
}

void LvSettingRollerPage2::page3_leave_done_cb(lv_anim_t *animation)
{
    auto *self = static_cast<LvSettingRollerPage2 *>(lv_anim_get_user_data(animation));
    if (self) self->finish_page3_transition(false);
}

void LvSettingRollerPage2::finish_page3_transition(bool entering)
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

void LvSettingRollerPage2::invoke_page3_animation_callback()
{
    auto animate_over_func = std::move(page3_animation_over_);
    page3_animation_over_  = nullptr;
    if (animate_over_func) animate_over_func();
}

void LvSettingRollerPage2::scroll_to_selected(lv_obj_t *cont, bool animated)
{
    if (!cont || item_count_ == 0) return;

    lv_obj_t *item = lv_obj_get_child(cont, selected_index);
    if (!item) return;

    lv_obj_scroll_to_view(item, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    lv_obj_send_event(cont, LV_EVENT_SCROLL, cont);
    update_arrow_visibility();
}

void LvSettingRollerPage2::select(int index)
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

void LvSettingRollerPage2::scroll_event_cb(lv_event_t *event)
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

void LvSettingRollerPage2::create_ui(lv_obj_t *parent)
{
    if (!parent) return;
    parent_ = parent;

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
        lv_obj_set_pos(right_arrow_, metric(LayoutMetric::PanelX) - lv_obj_get_width(right_arrow_) - 4,
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
            if (status_icon) request_status_refresh(row, it);
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
        lv_obj_set_style_text_font(hint_, settings_fonts::sans(16, LV_FREETYPE_FONT_STYLE_BOLD),
                                   LV_PART_MAIN);
        lv_obj_update_layout(hint_);
        lv_obj_set_pos(hint_, metric(LayoutMetric::PanelX) + metric(LayoutMetric::PanelW) - 6 - lv_obj_get_width(hint_),
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

void LvSettingRollerPage2::handle_key_event(lv_event_t *event)
{
    lv_obj_t *cont = lv_event_get_target(event);
    if (!cont || lv_event_get_code(event) != LV_EVENT_KEY) return;

    const uint32_t key = lv_event_get_key(event);
    if (power_warning_) {
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT || key == LV_KEY_ENTER || key == LV_KEY_RIGHT)
            close_power_warning();
        lv_event_stop_processing(event);
        return;
    }
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
        auto selected_node = std::next(parent_node_.begin(), selected_index);
        if (selected_node->page_factory) {
            LoadNextPage();
        } else if (selected_node->Componens_api) {
            if (selected_node->icon_enabled) ++selected_node->status_generation;
            selected_node->Componens_api(SettingApiActivate, this);

            if (selected_node->icon_enabled) {
                request_status_refresh(lv_obj_get_child(cont, selected_index), selected_node);
            }
        } else if (on_selected_page) {
            on_selected_page(selected_index);
        }
    }

    lv_event_stop_processing(event);
}
