/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_lora.hpp"

namespace lora_app_detail {

constexpr lv_coord_t kScreenWidth         = 320;
constexpr lv_coord_t kContentHeight       = 150;
constexpr uint32_t kViewTransitionMs      = 150;
constexpr uint32_t kMessageTitleHoldMs    = 3200;
constexpr uint32_t kMessageTitleHideMs    = 340;
constexpr lv_coord_t kMessageTitleShownY  = -8;
constexpr lv_coord_t kMessageTitleHiddenY = -29;
constexpr lv_coord_t kBubbleTailWidth     = 6;
constexpr lv_coord_t kBubbleTailDrop      = 3;

static const char *safe_text(const char *text, const char *fallback = "")
{
    return text && text[0] ? text : fallback;
}

}  // namespace lora_app_detail

void UILoraPage::set_visible(lv_obj_t *object, bool visible)
{
    if (!object) return;
    if (visible)
        lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *UILoraPage::make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height,
                                 lv_color_t color, lv_opa_t opacity, lv_coord_t radius)
{
    if (!parent) return nullptr;
    lv_obj_t *panel = lv_obj_create(parent);
    if (!panel) return nullptr;
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, opacity, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(panel, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

lv_obj_t *UILoraPage::make_plain_container(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                                           lv_coord_t height)
{
    return make_panel(parent, x, y, width, height, lv_color_hex(0x000000), LV_OPA_TRANSP, 0);
}

lv_obj_t *UILoraPage::make_label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                                 lv_coord_t height, const lv_font_t *font, lv_color_t color, lv_text_align_t align)
{
    if (!parent) return nullptr;
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}

lv_obj_t *UILoraPage::make_divider(lv_obj_t *parent, lv_coord_t y)
{
    return make_panel(parent, 8, y, 304, 1, lv_color_hex(0x25272B), LV_OPA_COVER, 0);
}

void UILoraPage::bubble_tail_draw_cb(lv_event_t *event) noexcept
{
    if (!event) return;
    lv_obj_t *row     = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    if (!row || !layer) return;
    lv_obj_t *bubble = lv_obj_get_child(row, 0);
    if (!bubble) return;

    lv_area_t area;
    lv_obj_get_coords(bubble, &area);
    lv_draw_triangle_dsc_t draw_dsc;
    lv_draw_triangle_dsc_init(&draw_dsc);
    draw_dsc.color             = lv_obj_get_style_bg_color(bubble, LV_PART_MAIN);
    draw_dsc.opa               = lv_obj_get_style_bg_opa(bubble, LV_PART_MAIN);
    lv_opa_t recursive_opacity = lv_obj_get_style_opa_recursive(bubble, LV_PART_MAIN);
    if (recursive_opacity < LV_OPA_MAX) draw_dsc.opa = LV_OPA_MIX2(draw_dsc.opa, recursive_opacity);

    static constexpr lv_coord_t TAIL_RISE     = 9;
    static constexpr lv_coord_t TAIL_SHOULDER = 10;
    bool outgoing                             = lv_obj_has_flag(row, LV_OBJ_FLAG_USER_1);
    lv_coord_t side_x                         = outgoing ? area.x2 : area.x1;
    lv_coord_t shoulder_x                     = outgoing ? area.x2 - TAIL_SHOULDER : area.x1 + TAIL_SHOULDER;
    lv_coord_t tip_x =
        outgoing ? area.x2 + lora_app_detail::kBubbleTailWidth : area.x1 - lora_app_detail::kBubbleTailWidth;
    draw_dsc.p[0] = {static_cast<lv_value_precise_t>(side_x), static_cast<lv_value_precise_t>(area.y2 - TAIL_RISE)};
    draw_dsc.p[1] = {static_cast<lv_value_precise_t>(shoulder_x), static_cast<lv_value_precise_t>(area.y2)};
    draw_dsc.p[2] = {static_cast<lv_value_precise_t>(tip_x),
                     static_cast<lv_value_precise_t>(area.y2 + lora_app_detail::kBubbleTailDrop)};
    lv_draw_triangle(layer, &draw_dsc);
}

void UILoraPage::update_page_indicator()
{
    if (!page_dots_[0] || !page_dots_[1]) return;
    bool messages_active = model_.view() == LoraView::MESSAGES;
    lv_obj_set_style_bg_color(page_dots_[0], lv_color_hex(messages_active ? 0xE4E4E4 : 0x4E5157),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(page_dots_[1], lv_color_hex(messages_active ? 0x4E5157 : 0xE4E4E4),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UILoraPage::update_info_content()
{
    if (!info_status_label_ || !info_status_dot_ || !info_device_value_ || !info_rssi_value_ || !info_snr_value_ ||
        !info_link_value_ || !info_diag_value_)
        return;
    const char *state_text = "RECEIVING";
    uint32_t state_color   = 0x69AD80;
    if (initialization_pending_) {
        state_text  = "INITIALIZING";
        state_color = 0xC9A45C;
    } else if (!lora_info_.hw_ready) {
        state_text  = "RADIO OFF";
        state_color = 0xD96C6C;
    } else if (lora_info_.tx_in_progress || lora_info_.tx_mode) {
        state_text  = lora_info_.tx_in_progress ? "SENDING" : "TX MODE";
        state_color = 0xC9A45C;
    }
    lv_label_set_text(info_status_label_, state_text);
    lv_obj_set_style_bg_color(info_status_dot_, lv_color_hex(state_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(info_device_value_, lora_app_detail::safe_text(lora_info_.spi_device, "Unavailable"));

    char value[64];
    std::snprintf(value, sizeof(value), "%.0f dBm", lora_info_.rssi);
    lv_label_set_text(info_rssi_value_, value);
    std::snprintf(value, sizeof(value), "%.1f dB", lora_info_.snr);
    lv_label_set_text(info_snr_value_, value);
    lv_label_set_text(info_link_value_,
                      lora_app_detail::safe_text(lora_info_.probe_display, "Link configuration unavailable"));
    lv_label_set_text(info_diag_value_, lora_app_detail::safe_text(lora_info_.diag, "No diagnostics"));
    const uint32_t diag_color = initialization_pending_ ? 0xC9A45C : (lora_info_.hw_ready ? 0x777B82 : 0xD96C6C);
    lv_obj_set_style_text_color(info_diag_value_, lv_color_hex(diag_color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UILoraPage::update_send_content()
{
    if (!send_input_label_ || !send_status_label_) return;
    std::string display = model_.tx_input() + "|";
    lv_label_set_text(send_input_label_, display.c_str());
    lv_obj_set_style_text_color(send_input_label_, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    bool has_status = !model_.send_status().empty();
    lv_label_set_text(send_status_label_, has_status ? model_.send_status().c_str() : "");
    set_visible(send_status_label_, has_status);
}

void UILoraPage::scroll_to_latest(lv_anim_enable_t animation)
{
    if (!message_list_ || !last_message_row_) {
        scroll_to_latest_pending_ = false;
        return;
    }
    lv_obj_update_layout(message_list_);
    lv_obj_scroll_to_view(last_message_row_, animation);
    scroll_to_latest_pending_ = false;
}

void UILoraPage::schedule_message_title_dismissal()
{
    if (!messages_title_ || message_title_timer_ || lv_obj_has_flag(messages_title_, LV_OBJ_FLAG_HIDDEN)) return;

    message_title_timer_ =
        lv_timer_create(&UILoraPage::static_message_title_timer_cb, lora_app_detail::kMessageTitleHoldMs, this);
    if (message_title_timer_) lv_timer_set_repeat_count(message_title_timer_, 1);
}

void UILoraPage::cancel_message_title_animation()
{
    if (message_title_timer_) {
        lv_timer_delete(message_title_timer_);
        message_title_timer_ = nullptr;
    }
    if (messages_title_) lv_anim_del(messages_title_, &UILoraPage::message_title_anim_exec_cb);
}

void UILoraPage::dismiss_message_title()
{
    if (!messages_title_ || lv_obj_has_flag(messages_title_, LV_OBJ_FLAG_HIDDEN)) return;

    if (message_title_timer_) {
        lv_timer_delete(message_title_timer_);
        message_title_timer_ = nullptr;
    }

    lv_anim_del(messages_title_, &UILoraPage::message_title_anim_exec_cb);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, messages_title_);
    lv_anim_set_values(&animation, lv_obj_get_y(messages_title_), lora_app_detail::kMessageTitleHiddenY);
    lv_anim_set_time(&animation, lora_app_detail::kMessageTitleHideMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, &UILoraPage::message_title_anim_exec_cb);
    lv_anim_set_user_data(&animation, messages_title_);
    lv_anim_set_completed_cb(&animation, &UILoraPage::hide_message_title_after_anim_cb);
    if (!lv_anim_start(&animation)) {
        message_title_anim_exec_cb(messages_title_, lora_app_detail::kMessageTitleHiddenY);
        set_visible(messages_title_, false);
    }
}

void UILoraPage::message_title_anim_exec_cb(void *object, int32_t y) noexcept
{
    if (!object) return;
    lv_obj_set_y(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(y));
}

void UILoraPage::hide_message_title_after_anim_cb(lv_anim_t *animation) noexcept
{
    if (!animation) return;
    auto *title = static_cast<lv_obj_t *>(lv_anim_get_user_data(animation));
    if (!lora_animation_callback_allowed(title)) return;
    message_title_anim_exec_cb(title, lora_app_detail::kMessageTitleHiddenY);
    set_visible(title, false);
}

void UILoraPage::view_opa_exec_cb(void *object, int32_t opacity) noexcept
{
    if (!lora_animation_callback_allowed(object)) return;
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(opacity),
                         LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UILoraPage::hide_view_after_fade_cb(lv_anim_t *animation) noexcept
{
    if (!animation) return;
    auto *view = static_cast<lv_obj_t *>(lv_anim_get_user_data(animation));
    if (!lora_animation_callback_allowed(view)) return;
    set_visible(view, false);
    lv_obj_set_style_opa(view, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UILoraPage::cancel_view_animation(lv_obj_t *view)
{
    if (view) lv_anim_del(view, view_opa_exec_cb);
}

void UILoraPage::cancel_view_animations()
{
    cancel_view_animation(messages_view_);
    cancel_view_animation(info_view_);
    cancel_view_animation(send_view_);
}

void UILoraPage::animate_view_opacity(lv_obj_t *view, lv_opa_t start, lv_opa_t end, bool hide_after_fade)
{
    if (!view) return;
    cancel_view_animation(view);
    set_visible(view, true);
    view_opa_exec_cb(view, start);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, view);
    lv_anim_set_values(&animation, start, end);
    lv_anim_set_time(&animation, lora_app_detail::kViewTransitionMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&animation, view_opa_exec_cb);
    if (hide_after_fade) {
        lv_anim_set_user_data(&animation, view);
        lv_anim_set_completed_cb(&animation, hide_view_after_fade_cb);
    }
    if (!lv_anim_start(&animation)) {
        view_opa_exec_cb(view, end);
        if (hide_after_fade) {
            set_visible(view, false);
            view_opa_exec_cb(view, LV_OPA_COVER);
        }
    }
}

void UILoraPage::transition_to_view(lv_obj_t *target)
{
    if (!target || target == active_view_) return;
    if (!active_view_) {
        lv_obj_t *views[] = {messages_view_, info_view_, send_view_};
        for (lv_obj_t *view : views) {
            cancel_view_animation(view);
            lv_obj_set_style_opa(view, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            set_visible(view, view == target);
        }
        active_view_ = target;
        return;
    }

    lv_obj_t *outgoing      = active_view_;
    lv_opa_t outgoing_start = lv_obj_get_style_opa(outgoing, LV_PART_MAIN);
    lv_opa_t incoming_start =
        lv_obj_has_flag(target, LV_OBJ_FLAG_HIDDEN) ? LV_OPA_TRANSP : lv_obj_get_style_opa(target, LV_PART_MAIN);
    animate_view_opacity(outgoing, outgoing_start, LV_OPA_TRANSP, true);
    animate_view_opacity(target, incoming_start, LV_OPA_COVER, false);
    active_view_ = target;
}

void UILoraPage::render_current_view()
{
    bool show_messages = model_.view() == LoraView::MESSAGES;
    bool show_info     = model_.view() == LoraView::INFO;
    bool show_send     = model_.view() == LoraView::SEND;
    if (show_info) update_info_content();
    if (show_send) update_send_content();
    transition_to_view(show_messages ? messages_view_ : (show_info ? info_view_ : send_view_));
    set_visible(page_indicator_, !show_send);
    if (!show_send) update_page_indicator();
    if (show_messages && scroll_to_latest_pending_) scroll_to_latest(LV_ANIM_OFF);
}

void UILoraPage::create_ui()
{
    page_root_ = make_panel(ui_APP_Container, 0, 0, lora_app_detail::kScreenWidth, lora_app_detail::kContentHeight,
                            lv_color_hex(0x0B0C0E), LV_OPA_COVER, 0);
    if (!page_root_) return;
    lv_obj_add_event_cb(page_root_, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
    create_messages_view();
    create_info_view();
    create_send_view();
    create_page_indicator();
    lv_obj_t *persistent_children[] = {
        empty_message_label_, empty_message_hint_label_, info_status_dot_,  info_status_label_,
        info_device_value_,   info_rssi_value_,          info_snr_value_,   info_link_value_,
        info_diag_value_,     send_input_bubble_,        send_input_label_, send_status_label_,
        send_cancel_button_,  send_confirm_button_,      page_dots_[0],     page_dots_[1],
        messages_title_};
    for (lv_obj_t *object : persistent_children) track_owned_handle(object);
}

void UILoraPage::create_messages_view()
{
    messages_view_ = make_plain_container(page_root_, 0, 0, 320, 150);
    if (!messages_view_) return;
    lv_obj_add_event_cb(messages_view_, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
    message_list_ = make_plain_container(messages_view_, 0, 0, 320, 150);
    if (!message_list_) return;
    lv_obj_add_event_cb(message_list_, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_flex_flow(message_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(message_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(message_list_, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(message_list_, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(message_list_, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(message_list_, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(message_list_, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(message_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(message_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(message_list_, LV_SCROLLBAR_MODE_OFF);

    empty_message_label_      = make_label(messages_view_, "No messages yet", 0, 50, 320, 16, &lv_font_montserrat_12,
                                           lv_color_hex(0xB2B2B2), LV_TEXT_ALIGN_CENTER);
    empty_message_hint_label_ = make_label(messages_view_, "Type anything to send", 0, 68, 320, 14,
                                           &lv_font_montserrat_12, lv_color_hex(0x5FE492), LV_TEXT_ALIGN_CENTER);
    messages_title_           = make_panel(messages_view_, 108, lora_app_detail::kMessageTitleShownY, 104, 28,
                                           lv_color_hex(0x0B0C0E), LV_OPA_COVER, 8);
    if (messages_title_)
        make_label(messages_title_, "Messages", 0, 8, 104, 18, &lv_font_montserrat_14, lv_color_hex(0xE4E4E4),
                   LV_TEXT_ALIGN_CENTER);
}

void UILoraPage::create_info_view()
{
    info_view_ = make_plain_container(page_root_, 0, 0, 320, 150);
    if (!info_view_) return;
    lv_obj_add_event_cb(info_view_, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
    make_label(info_view_, "LoRa Info", 0, 0, 320, 18, &lv_font_montserrat_14, lv_color_hex(0xE4E4E4),
               LV_TEXT_ALIGN_CENTER);
    info_status_dot_ = make_panel(info_view_, 8, 23, 6, 6, lv_color_hex(0x69AD80), LV_OPA_COVER, LV_RADIUS_CIRCLE);
    info_status_label_ =
        make_label(info_view_, "", 20, 20, 180, 16, &lv_font_montserrat_10, lv_color_hex(0xAEB2B8), LV_TEXT_ALIGN_LEFT);
    make_label(info_view_, "CLIENT", 240, 20, 72, 16, &lv_font_montserrat_10, lv_color_hex(0x777B82),
               LV_TEXT_ALIGN_RIGHT);
    make_divider(info_view_, 41);
    make_label(info_view_, "DEVICE", 8, 48, 148, 11, &lv_font_montserrat_10, lv_color_hex(0x777B82),
               LV_TEXT_ALIGN_LEFT);
    make_label(info_view_, "RSSI", 166, 48, 66, 11, &lv_font_montserrat_10, lv_color_hex(0x777B82), LV_TEXT_ALIGN_LEFT);
    make_label(info_view_, "SNR", 240, 48, 72, 11, &lv_font_montserrat_10, lv_color_hex(0x777B82), LV_TEXT_ALIGN_LEFT);
    info_device_value_ =
        make_label(info_view_, "", 8, 60, 148, 17, &lv_font_montserrat_12, lv_color_hex(0xDDE0E4), LV_TEXT_ALIGN_LEFT);
    info_rssi_value_ =
        make_label(info_view_, "", 166, 60, 66, 17, &lv_font_montserrat_12, lv_color_hex(0xDDE0E4), LV_TEXT_ALIGN_LEFT);
    info_snr_value_ =
        make_label(info_view_, "", 240, 60, 72, 17, &lv_font_montserrat_12, lv_color_hex(0xDDE0E4), LV_TEXT_ALIGN_LEFT);
    if (info_device_value_) lv_label_set_long_mode(info_device_value_, LV_LABEL_LONG_DOT);
    make_divider(info_view_, 81);
    make_label(info_view_, "LINK", 8, 88, 304, 11, &lv_font_montserrat_10, lv_color_hex(0x777B82), LV_TEXT_ALIGN_LEFT);
    info_link_value_ =
        make_label(info_view_, "", 8, 100, 304, 16, &lv_font_montserrat_12, lv_color_hex(0xDDE0E4), LV_TEXT_ALIGN_LEFT);
    info_diag_value_ =
        make_label(info_view_, "", 8, 120, 304, 13, &lv_font_montserrat_10, lv_color_hex(0x777B82), LV_TEXT_ALIGN_LEFT);
    if (info_link_value_) lv_label_set_long_mode(info_link_value_, LV_LABEL_LONG_DOT);
    if (info_diag_value_) lv_label_set_long_mode(info_diag_value_, LV_LABEL_LONG_DOT);
}

void UILoraPage::create_send_view()
{
    send_view_ = make_plain_container(page_root_, 0, 0, 320, 150);
    if (!send_view_) return;
    lv_obj_add_event_cb(send_view_, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
    make_label(send_view_, "New Message", 0, 0, 320, 18, &lv_font_montserrat_14, lv_color_hex(0xE4E4E4),
               LV_TEXT_ALIGN_CENTER);
    send_input_bubble_ = make_panel(send_view_, 0, 0, 286, 80, lv_color_hex(0x555555), LV_OPA_COVER, 8);
    if (send_input_bubble_) lv_obj_align(send_input_bubble_, LV_ALIGN_CENTER, 0, -12);
    send_input_label_  = make_label(send_input_bubble_, "", 10, 8, 266, 64, &lv_font_montserrat_14,
                                    lv_color_hex(0xFFFFFF), LV_TEXT_ALIGN_LEFT);
    send_status_label_ = make_label(send_input_bubble_, "", 10, 54, 266, 16, &lv_font_montserrat_14,
                                    lv_color_hex(0xFED40D), LV_TEXT_ALIGN_RIGHT);
    set_visible(send_status_label_, false);
    send_cancel_button_  = make_action_button(send_view_, 83, 113, 110, "ESC: Cancel", lv_color_hex(0x6D6D6D),
                                              lv_color_hex(0xF3F3F3), &UILoraPage::static_cancel_button_cb);
    send_confirm_button_ = make_action_button(send_view_, 203, 113, 100, "Enter: Send", lv_color_hex(0xFED40D),
                                              lv_color_hex(0x5E4D00), &UILoraPage::static_send_button_cb);
}

lv_obj_t *UILoraPage::make_action_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                                         const char *text, lv_color_t background, lv_color_t foreground,
                                         lv_event_cb_t callback)
{
    lv_obj_t *button = make_panel(parent, x, y, width, 26, background, LV_OPA_COVER, 5);
    if (!button) return nullptr;
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    lv_obj_t *label = make_label(button, text, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT, &lv_font_montserrat_14,
                                 foreground, LV_TEXT_ALIGN_CENTER);
    if (label) lv_obj_center(label);
    return button;
}

void UILoraPage::create_page_indicator()
{
    page_indicator_ = make_panel(page_root_, 141, 137, 38, 24, lv_color_hex(0x0B0C0E), LV_OPA_COVER, 7);
    if (!page_indicator_) return;
    lv_obj_add_event_cb(page_indicator_, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
    page_dots_[0] = make_panel(page_indicator_, 11, 3, 5, 5, lv_color_hex(0xE4E4E4), LV_OPA_COVER, LV_RADIUS_CIRCLE);
    page_dots_[1] = make_panel(page_indicator_, 22, 3, 5, 5, lv_color_hex(0x4E5157), LV_OPA_COVER, LV_RADIUS_CIRCLE);
}

void UILoraPage::bind_events()
{
    if (root_screen_) lv_obj_add_event_cb(root_screen_, &UILoraPage::static_key_event_cb, LV_EVENT_ALL, this);
}

void UILoraPage::track_owned_handle(lv_obj_t *object)
{
    if (object) lv_obj_add_event_cb(object, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
}

bool UILoraPage::ui_ready() const
{
    return lora_page_controls_ready(
        page_root_, messages_view_, message_list_, empty_message_label_, empty_message_hint_label_, info_view_,
        info_status_dot_, info_status_label_, info_device_value_, info_rssi_value_, info_snr_value_, info_link_value_,
        info_diag_value_, send_view_, send_input_bubble_, send_input_label_, send_status_label_, send_cancel_button_,
        send_confirm_button_, page_indicator_, page_dots_[0], page_dots_[1]);
}

void UILoraPage::detach_delete_callbacks()
{
    lv_obj_t *objects[] = {message_list_,
                           messages_view_,
                           empty_message_label_,
                           empty_message_hint_label_,
                           messages_title_,
                           info_view_,
                           info_status_dot_,
                           info_status_label_,
                           info_device_value_,
                           info_rssi_value_,
                           info_snr_value_,
                           info_link_value_,
                           info_diag_value_,
                           send_view_,
                           send_input_bubble_,
                           send_input_label_,
                           send_status_label_,
                           send_cancel_button_,
                           send_confirm_button_,
                           page_indicator_,
                           page_dots_[0],
                           page_dots_[1],
                           page_root_};
    for (lv_obj_t *object : objects)
        if (object) lv_obj_remove_event_cb_with_user_data(object, static_owned_obj_delete_cb, this);
}

void UILoraPage::clear_deleted_handles(lv_obj_t *deleted)
{
    if (!deleted) return;
    if (deleted == messages_title_) {
        lv_anim_del(deleted, &UILoraPage::message_title_anim_exec_cb);
        messages_title_ = nullptr;
    }
    if (deleted == empty_message_label_) empty_message_label_ = nullptr;
    if (deleted == empty_message_hint_label_) empty_message_hint_label_ = nullptr;
    if (deleted == info_status_dot_) info_status_dot_ = nullptr;
    if (deleted == info_status_label_) info_status_label_ = nullptr;
    if (deleted == info_device_value_) info_device_value_ = nullptr;
    if (deleted == info_rssi_value_) info_rssi_value_ = nullptr;
    if (deleted == info_snr_value_) info_snr_value_ = nullptr;
    if (deleted == info_link_value_) info_link_value_ = nullptr;
    if (deleted == info_diag_value_) info_diag_value_ = nullptr;
    if (deleted == send_input_bubble_) send_input_bubble_ = nullptr;
    if (deleted == send_input_label_) send_input_label_ = nullptr;
    if (deleted == send_status_label_) send_status_label_ = nullptr;
    if (deleted == send_cancel_button_) send_cancel_button_ = nullptr;
    if (deleted == send_confirm_button_) send_confirm_button_ = nullptr;
    if (deleted == page_dots_[0]) page_dots_[0] = nullptr;
    if (deleted == page_dots_[1]) page_dots_[1] = nullptr;
    if (deleted == message_list_) {
        message_list_     = nullptr;
        last_message_row_ = nullptr;
    }
    if (deleted == messages_view_) {
        cancel_message_title_animation();
        messages_view_            = nullptr;
        message_list_             = nullptr;
        messages_title_           = nullptr;
        empty_message_label_      = nullptr;
        empty_message_hint_label_ = nullptr;
        last_message_row_         = nullptr;
    }
    if (deleted == info_view_) {
        info_view_         = nullptr;
        info_status_dot_   = nullptr;
        info_status_label_ = nullptr;
        info_device_value_ = nullptr;
        info_rssi_value_   = nullptr;
        info_snr_value_    = nullptr;
        info_link_value_   = nullptr;
        info_diag_value_   = nullptr;
    }
    if (deleted == send_view_) {
        send_view_           = nullptr;
        send_input_bubble_   = nullptr;
        send_input_label_    = nullptr;
        send_status_label_   = nullptr;
        send_cancel_button_  = nullptr;
        send_confirm_button_ = nullptr;
    }
    if (deleted == page_indicator_) {
        page_indicator_ = nullptr;
        page_dots_[0]   = nullptr;
        page_dots_[1]   = nullptr;
    }
    if (active_view_ == deleted) active_view_ = nullptr;
    if (deleted == page_root_) {
        page_root_                = nullptr;
        messages_view_            = nullptr;
        message_list_             = nullptr;
        empty_message_label_      = nullptr;
        empty_message_hint_label_ = nullptr;
        last_message_row_         = nullptr;
        info_view_                = nullptr;
        info_status_dot_          = nullptr;
        info_status_label_        = nullptr;
        info_device_value_        = nullptr;
        info_rssi_value_          = nullptr;
        info_snr_value_           = nullptr;
        info_link_value_          = nullptr;
        info_diag_value_          = nullptr;
        send_view_                = nullptr;
        send_input_bubble_        = nullptr;
        send_input_label_         = nullptr;
        send_status_label_        = nullptr;
        send_cancel_button_       = nullptr;
        send_confirm_button_      = nullptr;
        page_indicator_           = nullptr;
        page_dots_[0]             = nullptr;
        page_dots_[1]             = nullptr;
        active_view_              = nullptr;
        app_active_               = false;
        cancel_message_title_animation();
        messages_title_ = nullptr;
        poll_timer_.stop();
    }
    if (!ui_ready()) {
        app_active_ = false;
        poll_timer_.stop();
    }
}

void UILoraPage::static_owned_obj_delete_cb(lv_event_t *event) noexcept
{
    try {
        if (!event ||
            !lora_owned_delete_callback_allowed(lv_event_get_target(event), lv_event_get_current_target(event)))
            return;
        auto *self    = static_cast<UILoraPage *>(lv_event_get_user_data(event));
        auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (self) self->clear_deleted_handles(deleted);
    } catch (...) {
        auto *self = event ? static_cast<UILoraPage *>(lv_event_get_user_data(event)) : nullptr;
        if (self) self->app_active_ = false;
    }
}

lv_obj_t *UILoraPage::append_message_row(const LoraChatMessage &message)
{
    if (!message_list_) return nullptr;
    char metadata[64] = "";
    if (!message.outgoing) std::snprintf(metadata, sizeof(metadata), "%.0f dBm  /  %.1f dB", message.rssi, message.snr);

    static constexpr int32_t HORIZONTAL_PADDING = 10;
    static constexpr int32_t MAX_TEXT_WIDTH     = 224;
    static constexpr int32_t MAX_BUBBLE_WIDTH   = 244;
    lv_point_t text_size{};
    lv_text_get_size(&text_size, message.text.c_str(), &lv_font_montserrat_12, 0, 0, MAX_TEXT_WIDTH, LV_TEXT_FLAG_NONE);
    int32_t content_width = text_size.x;
    if (metadata[0]) {
        lv_point_t metadata_size{};
        lv_text_get_size(&metadata_size, metadata, &lv_font_montserrat_10, 0, 0, MAX_TEXT_WIDTH, LV_TEXT_FLAG_NONE);
        content_width = std::max(content_width, metadata_size.x);
    }
    int32_t bubble_width =
        std::max<int32_t>(64, std::min<int32_t>(MAX_BUBBLE_WIDTH, content_width + HORIZONTAL_PADDING * 2));

    lv_obj_t *row = make_plain_container(message_list_, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    if (!row) return nullptr;
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, message.outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    if (message.outgoing) lv_obj_add_flag(row, LV_OBJ_FLAG_USER_1);
    lv_obj_add_event_cb(row, bubble_tail_draw_cb, LV_EVENT_DRAW_MAIN_END, nullptr);

    lv_obj_t *bubble = make_panel(row, 0, 0, bubble_width, LV_SIZE_CONTENT,
                                  lv_color_hex(message.outgoing ? 0x3FCC75 : 0xCCCCCC), LV_OPA_COVER, 8);
    if (!bubble) return row;
    lv_obj_set_style_margin_left(bubble, message.outgoing ? 0 : lora_app_detail::kBubbleTailWidth,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_margin_right(bubble, message.outgoing ? lora_app_detail::kBubbleTailWidth : 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_margin_bottom(bubble, lora_app_detail::kBubbleTailDrop, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(bubble, HORIZONTAL_PADDING, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bubble, HORIZONTAL_PADDING, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bubble, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bubble, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bubble, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    make_label(bubble, message.text.c_str(), 0, 0, bubble_width - HORIZONTAL_PADDING * 2, LV_SIZE_CONTENT,
               &lv_font_montserrat_12, lv_color_hex(0x000000), LV_TEXT_ALIGN_LEFT);
    if (metadata[0])
        make_label(bubble, metadata, 0, 0, bubble_width - HORIZONTAL_PADDING * 2, LV_SIZE_CONTENT,
                   &lv_font_montserrat_10, lv_color_hex(0x7E7E7E), LV_TEXT_ALIGN_RIGHT);
    return row;
}
