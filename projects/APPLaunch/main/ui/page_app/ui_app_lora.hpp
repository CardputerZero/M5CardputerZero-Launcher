/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "../launcher_ui_app_page.hpp"
#include "../model/lora_page_model.hpp"
#include "../model/lora_page_contract.hpp"
#include "../model/page_timer_lifecycle.hpp"
#include "input_keys.h"
#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>

namespace lora_app_detail {
struct LoraInitializationState;
}

class UILoraPage : public AppPage {
public:
    UILoraPage();
    ~UILoraPage() override;

private:
    LoraPageModel model_;
    bool app_active_               = false;
    bool initialization_pending_   = false;
    bool scroll_to_latest_pending_ = false;
    cp0_lora_info_t lora_info_{};
    std::shared_ptr<lora_app_detail::LoraInitializationState> initialization_state_;

    PageTimerLifecycle<lv_timer_t *> poll_timer_;
    lv_timer_t *message_title_timer_    = nullptr;
    lv_obj_t *page_root_                = nullptr;
    lv_obj_t *messages_view_            = nullptr;
    lv_obj_t *message_list_             = nullptr;
    lv_obj_t *messages_title_           = nullptr;
    lv_obj_t *empty_message_label_      = nullptr;
    lv_obj_t *empty_message_hint_label_ = nullptr;
    lv_obj_t *last_message_row_         = nullptr;
    lv_obj_t *info_view_                = nullptr;
    lv_obj_t *info_status_dot_          = nullptr;
    lv_obj_t *info_status_label_        = nullptr;
    lv_obj_t *info_device_value_        = nullptr;
    lv_obj_t *info_rssi_value_          = nullptr;
    lv_obj_t *info_snr_value_           = nullptr;
    lv_obj_t *info_link_value_          = nullptr;
    lv_obj_t *info_diag_value_          = nullptr;
    lv_obj_t *send_view_                = nullptr;
    lv_obj_t *send_input_bubble_        = nullptr;
    lv_obj_t *send_input_label_         = nullptr;
    lv_obj_t *send_status_label_        = nullptr;
    lv_obj_t *send_cancel_button_       = nullptr;
    lv_obj_t *send_confirm_button_      = nullptr;
    lv_obj_t *page_indicator_           = nullptr;
    lv_obj_t *page_dots_[2]             = {nullptr, nullptr};
    lv_obj_t *active_view_              = nullptr;

    static void set_visible(lv_obj_t *object, bool visible);

    static lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height,
                                lv_color_t color, lv_opa_t opacity, lv_coord_t radius);

    static lv_obj_t *make_plain_container(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                                          lv_coord_t height);

    static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                                lv_coord_t height, const lv_font_t *font, lv_color_t color, lv_text_align_t align);

    static lv_obj_t *make_divider(lv_obj_t *parent, lv_coord_t y);

    static void bubble_tail_draw_cb(lv_event_t *event) noexcept;

    void create_ui();
    void create_messages_view();
    void create_info_view();
    void create_send_view();

    lv_obj_t *make_action_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, const char *text,
                                 lv_color_t background, lv_color_t foreground, lv_event_cb_t callback);
    void create_page_indicator();
    void bind_events();
    void track_owned_handle(lv_obj_t *object);
    bool ui_ready() const;
    void detach_delete_callbacks();
    void clear_deleted_handles(lv_obj_t *deleted);
    static void static_owned_obj_delete_cb(lv_event_t *event) noexcept;

    void init_lora();
    void start_lora_initialization();
    bool consume_lora_initialization();
    bool refresh_lora_info(bool poll);
    void update_page_indicator();
    void update_info_content();
    void update_send_content();
    void scroll_to_latest(lv_anim_enable_t animation);
    void schedule_message_title_dismissal();
    void dismiss_message_title();
    void cancel_message_title_animation();
    static void message_title_anim_exec_cb(void *object, int32_t y) noexcept;
    static void hide_message_title_after_anim_cb(lv_anim_t *animation) noexcept;
    static void view_opa_exec_cb(void *object, int32_t opacity) noexcept;
    static void hide_view_after_fade_cb(lv_anim_t *animation) noexcept;
    static void cancel_view_animation(lv_obj_t *view);
    void cancel_view_animations();
    static void animate_view_opacity(lv_obj_t *view, lv_opa_t start, lv_opa_t end, bool hide_after_fade);
    void transition_to_view(lv_obj_t *target);
    void render_current_view();

    lv_obj_t *append_message_row(const LoraChatMessage &message);

    void append_chat_message(const char *text, bool outgoing, float rssi, float snr);
    void open_send_view(uint32_t first_key);
    void scroll_messages(int32_t amount);
    void cancel_send();
    bool handle_send_key(uint32_t key);
    bool handle_navigation_key(uint32_t key);
    bool handle_key(uint32_t key);
    void append_text_key(uint32_t key);
    void send_current_text();
    uint32_t normalize_key(const key_item *key_event) const;
    void on_key_event(lv_event_t *event);
    void on_poll_timer();
    static void static_cancel_button_cb(lv_event_t *event) noexcept;
    static void static_send_button_cb(lv_event_t *event) noexcept;
    static void static_key_event_cb(lv_event_t *event) noexcept;
    static void static_poll_timer_cb(lv_timer_t *timer) noexcept;
    static void static_message_title_timer_cb(lv_timer_t *timer) noexcept;
};
