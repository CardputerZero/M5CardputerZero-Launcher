/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_setup.hpp"

void UISetupPage::event_handler_init()
{
    lv_obj_add_event_cb(root_screen_, UISetupPage::static_handler, LV_EVENT_ALL, this);
}

void UISetupPage::static_handler(lv_event_t *event) noexcept
{
    if (!event) return;
    auto *self = static_cast<UISetupPage *>(lv_event_get_user_data(event));
    if (!self || !setup_root_callback_allowed(
            lv_event_get_current_target(event), self->root_screen_)) return;
    try {
        self->on_event(event);
    } catch (...) {
        try {
            self->on_root_deleted();
        } catch (...) {
            self->lifecycle_.root_deleted();
        }
    }
}

void UISetupPage::on_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_DELETE &&
        lv_event_get_target(event) == lv_event_get_current_target(event)) {
        on_root_deleted();
        return;
    }
    if (!lifecycle_.active()) return;

    bool released = launcher_ui::events::is_key_released(event);
    bool pressed = launcher_ui::events::is_key_pressed(event);
    if (!released && !pressed) return;

    auto *item = static_cast<key_item *>(lv_event_get_param(event));
    if (!item) return;
    cur_elm_ = item;
    const bool text_input = view_state_ == ViewState::WIFI_PW ||
                            view_state_ == ViewState::WIFI_SSID ||
                            view_state_ == ViewState::BT_ALIAS;
    uint32_t key = text_input ? item->key_code : remap_fzxc(item->key_code);

    if (developer_.status_active()) {
        if (released) developer_.handle_status_key(*this, key);
        return;
    }
    if (rtc_.write_confirm_active()) {
        if (released) rtc_.handle_write_confirm_key(*this, key);
        return;
    }

    if (pressed) {
        if (key != KEY_UP && key != KEY_DOWN) return;
        uint32_t now = lv_tick_get();
        const bool wifi_repeat = view_state_ == ViewState::WIFI_LIST &&
                                 item->key_state == KBD_KEY_REPEATED;
        const uint32_t repeat_interval = view_state_ == ViewState::WIFI_LIST
            ? SetupWifiListViewModel::KEY_REPEAT_INTERVAL_MS : REPEAT_INTERVAL_MS;
        if ((view_state_ != ViewState::WIFI_LIST || wifi_repeat) &&
            now - last_repeat_tick_ < repeat_interval) return;
        last_repeat_tick_ = now;
    } else if (key == KEY_UP || key == KEY_DOWN) {
        // WiFi navigation is handled on press/repeat. A release must not delay
        // the next physical press.
        if (view_state_ == ViewState::WIFI_LIST) return;
        uint32_t now = lv_tick_get();
        if (now - last_repeat_tick_ < REPEAT_INTERVAL_MS) return;
        last_repeat_tick_ = now;
    }

    switch (view_state_) {
    case ViewState::MAIN:
        handle_main_key(key);
        break;
    case ViewState::SUB:
        handle_sub_key(key);
        break;
    case ViewState::VALUE_SELECT:
        handle_value_key(key);
        break;
    case ViewState::HELP:
        if (released) setting::Help::handle_key(*this, key);
        break;
    case ViewState::WIFI_LIST:
        if (pressed && (key == KEY_UP || key == KEY_DOWN))
            wifi_.handle_list_key(*this, key);
        else if (released && key != KEY_UP && key != KEY_DOWN)
            wifi_.handle_list_key(*this, key);
        break;
    case ViewState::WIFI_PW:
        if (released) wifi_.handle_pw_key(*this, key);
        break;
    case ViewState::WIFI_SSID:
        if (released) wifi_.handle_ssid_key(*this, key);
        break;
    case ViewState::WIFI_FORGET_CONFIRM:
        if (released) wifi_.handle_forget_key(*this, key);
        break;
    case ViewState::WIFI_POWER_WARNING:
        if (released) wifi_.handle_power_warning_key(*this, key);
        break;
    case ViewState::BT_LIST:
        if (!bluetooth_ui_) return;
        if (pressed && (key == KEY_UP || key == KEY_DOWN))
            bluetooth_ui_->handle_list_key(*this, key);
        else if (released && key != KEY_UP && key != KEY_DOWN)
            bluetooth_ui_->handle_list_key(*this, key);
        break;
    case ViewState::BT_ALIAS:
        if (released && bluetooth_ui_) bluetooth_ui_->handle_alias_key(*this, key);
        break;
    case ViewState::BT_POWER_WARNING:
        if (released && bluetooth_ui_) bluetooth_ui_->handle_power_warning_key(*this, key);
        break;
    case ViewState::SOUNDCARD_CARDS:
        if (pressed && (key == KEY_UP || key == KEY_DOWN))
            soundcard_.handle_cards_key(*this, key);
        else if (released && key != KEY_UP && key != KEY_DOWN)
            soundcard_.handle_cards_key(*this, key);
        break;
    case ViewState::SOUNDCARD_CONTROLS:
        if (pressed && (key == KEY_UP || key == KEY_DOWN))
            soundcard_.handle_controls_key(*this, key);
        else if (released && key != KEY_UP && key != KEY_DOWN)
            soundcard_.handle_controls_key(*this, key);
        break;
    case ViewState::SOUNDCARD_DETAIL:
        if (released) soundcard_.handle_detail_key(*this, key);
        break;
    case ViewState::USB_GUIDE:
        if (released) developer_.handle_usb_guide_key(*this, key);
        break;
    case ViewState::ADB_PAIR:
        if (released) developer_.handle_pair_key(*this, key);
        break;
    case ViewState::ADB_AUTHORIZATIONS:
        if (released) developer_.handle_authorizations_key(*this, key);
        break;
    }
}

void UISetupPage::on_root_deleted()
{
    if (!lifecycle_.active()) return;
    cancel_scroll_animations();
    lifecycle_.root_deleted();
    stop_power_timer();
    stop_update_timer();
    stop_volume_preview_timer();
    speaker_.cancel_preview(*this);
    wifi_.shutdown();
    info_.stop_timer();
    bluetooth_ui_.reset();
    developer_.shutdown();
    rtc_.shutdown();
    soundcard_.shutdown();
    confirm_controller_.cancel();

    ui_obj_.clear();
    for (auto *&label : row_labels_) label = nullptr;
    sel_bg_ = nullptr;
    hint_lbl_ = nullptr;
    arrow_up_obj_ = nullptr;
    arrow_down_obj_ = nullptr;
    cur_elm_ = nullptr;
}

uint32_t UISetupPage::remap_fzxc(uint32_t key)
{
    switch (key) {
    case KEY_F: return KEY_UP;
    case KEY_X: return KEY_DOWN;
    case KEY_Z: return KEY_LEFT;
    case KEY_C: return KEY_RIGHT;
    default: return key;
    }
}

void UISetupPage::handle_main_key(uint32_t key)
{
    switch (key) {
    case KEY_UP:
        animate_scroll(-1);
        break;
    case KEY_DOWN:
        animate_scroll(1);
        break;
    case KEY_ENTER:
    case KEY_RIGHT: {
        play_enter();
        MenuItem &item = menu_items_[selected_idx_];
        if (item.on_enter) item.on_enter();
        if (!item.sub_items.empty()) {
            int sub_count = static_cast<int>(item.sub_items.size());
            model_.enter_sub(sub_count, ROW_CENTER, item.default_sub_selection);
            build_sub_view();
        }
        break;
    }
    case KEY_ESC:
        play_back();
        if (navigate_home) navigate_home();
        break;
    default:
        break;
    }
}

void UISetupPage::handle_sub_key(uint32_t key)
{
    MenuItem &item = menu_items_[selected_idx_];
    int sub_count = static_cast<int>(item.sub_items.size());
    switch (key) {
    case KEY_UP:
        if (model_.move_sub(-1, sub_count)) {
            build_sub_view();
        }
        break;
    case KEY_DOWN:
        if (model_.move_sub(1, sub_count)) {
            build_sub_view();
        }
        break;
    case KEY_ENTER:
    case KEY_RIGHT:
        play_enter();
        if (sub_selected_idx_ < sub_count) {
            SubItem &sub = item.sub_items[sub_selected_idx_];
            if (sub.is_toggle) {
                sub.toggle_state = !sub.toggle_state;
                if (sub.action) sub.action();
                build_sub_view();
            } else if (sub.action) {
                sub.action();
            }
        }
        break;
    case KEY_ESC:
    case KEY_LEFT:
        play_back();
        info_.stop_timer();
        if (item.label == "Bluetooth")
            release_bluetooth_ui();
        if (item.label == "RTC" && rtc_.is_dirty()) {
            rtc_.show_write_confirm(*this);
            break;
        }
        model_.leave_to_main();
        build_main_view();
        break;
    default:
        if (item.custom_key_handler) item.custom_key_handler(key);
        break;
    }
}

void UISetupPage::handle_value_key(uint32_t key)
{
    switch (key) {
    case KEY_UP:
        if (model_.move_value(-1)) {
            build_value_view();
            if (val_title_ == "Volume" && speaker_.preview_value(*this))
                schedule_volume_preview();
        }
        break;
    case KEY_DOWN:
        if (model_.move_value(1)) {
            build_value_view();
            if (val_title_ == "Volume" && speaker_.preview_value(*this))
                schedule_volume_preview();
        }
        break;
    case KEY_ENTER:
    case KEY_RIGHT:
        stop_volume_preview_timer();
        if (apply_value_selection() && val_title_ == "Volume") play_enter();
        if (val_title_ == "Reboot?" || val_title_ == "Shutdown?" || val_title_ == "Run Setup?") {
            lv_obj_t *container = ui_obj_["list_cont"];
            lv_obj_clean(container);
            lv_obj_t *label = lv_label_create(container);
            lv_label_set_text(label,
                              val_title_ == "Shutdown?" ? "Shutting down..." : "Rebooting...");
            lv_obj_center(label);
            lv_obj_set_style_text_color(label, lv_color_hex(0x58A6FF), LV_PART_MAIN);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_refr_now(nullptr);
            break;
        }
        model_.leave_to_sub();
        transition_back_level();
        break;
    case KEY_ESC:
    case KEY_LEFT:
        stop_volume_preview_timer();
        if (val_title_ == "Volume") speaker_.cancel_preview(*this);
        confirm_controller_.cancel();
        model_.leave_to_sub();
        transition_back_level();
        break;
    default:
        break;
    }
}
