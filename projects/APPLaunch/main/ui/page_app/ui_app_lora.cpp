/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_lora.hpp"

#include <exception>
#include <mutex>
#include <thread>

namespace lora_app_detail {

struct LoraInitializationState {
    std::mutex mutex;
    bool done        = false;
    int init_code    = -1;
    int info_code    = -1;
    int receive_code = -1;
    cp0_lora_info_t info{};
};

constexpr uint32_t kPollIntervalMs   = 300;
constexpr int32_t kMessageScrollStep = 36;
constexpr uint32_t kInitRetryIntervalMs = 3000;

static bool is_printable_ascii(uint32_t key)
{
    return key >= 0x20 && key <= 0x7e;
}

static char key_to_ascii(uint32_t key)
{
    return is_printable_ascii(key) ? static_cast<char>(key) : '\0';
}

static bool is_modifier_event(const key_item *key_event)
{
    if (!key_event) return true;

    switch (key_event->key_code) {
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
        case KEY_LEFTCTRL:
        case KEY_LEFTALT:
            return true;
#ifdef KEY_RIGHTCTRL
        case KEY_RIGHTCTRL:
            return true;
#endif
#ifdef KEY_RIGHTALT
        case KEY_RIGHTALT:
            return true;
#endif
#ifdef KEY_LEFTMETA
        case KEY_LEFTMETA:
            return true;
#endif
#ifdef KEY_RIGHTMETA
        case KEY_RIGHTMETA:
            return true;
#endif
#ifdef KEY_FN
        case KEY_FN:
            return true;
#endif
#ifdef KEY_COMPOSE
        case KEY_COMPOSE:
            return true;
#endif
        default:
            break;
    }

    // Some firmware revisions expose modifiers through a custom evdev code but
    // retain the standard XKB name. Do not let those events enter the chat.
    static constexpr const char *kModifierNames[] = {
        "Shift_L",     "Shift_R",          "Control_L", "Control_R", "Alt_L",     "Alt_R",
        "Meta_L",      "Meta_R",           "Super_L",   "Super_R",   "Multi_key", "Menu",
        "Mode_switch", "ISO_Level3_Shift", "Fn",        "FN",        "Sym",       "SYM",
    };
    for (const char *name : kModifierNames) {
        if (std::strcmp(key_event->sym_name, name) == 0) return true;
    }
    return false;
}

static uint32_t printable_text_key(const key_item *key_event)
{
    if (!key_event) return 0;

    // utf8 is the final text produced by xkb/custom TCA mappings. Prefer it
    // over codepoint: a malformed modifier mapping can leave codepoint set to
    // a printable symbol even though the event is actually a control key.
    const unsigned char first = static_cast<unsigned char>(key_event->utf8[0]);
    if (is_printable_ascii(first) && key_event->utf8[1] == '\0') return first;
    return is_printable_ascii(key_event->codepoint) ? key_event->codepoint : 0;
}

static int call_lora_api(const std::list<std::string> &args, cp0_lora_info_t *info = nullptr)
{
    int result = -1;
    cp0_lora_info_t response{};
    bool response_valid = false;
    cp0_signal_lora_api(args, [&](int code, std::string data) {
        result = code;
        if (info && lora_info_response_valid(code, data.size(), sizeof(response))) {
            std::memcpy(&response, data.data(), sizeof(response));
            response_valid = true;
        }
    });
    if (info && response_valid) *info = response;
    if (info && result == 0 && !response_valid) return -1;
    return result;
}

static bool is_menu_prev_key(uint32_t key)
{
    return key == LV_KEY_LEFT || key == LV_KEY_PREV || key == 'z' || key == 'Z';
}

static bool is_menu_next_key(uint32_t key)
{
    return key == LV_KEY_RIGHT || key == LV_KEY_NEXT || key == 'c' || key == 'C';
}

}  // namespace lora_app_detail

namespace {

void run_lora_initialization(const std::shared_ptr<lora_app_detail::LoraInitializationState> &state) noexcept
{
    int init_code    = -1;
    int info_code    = -1;
    int receive_code = -1;
    cp0_lora_info_t info{};

    try {
        init_code = lora_app_detail::call_lora_api({"Init"});
        info_code = lora_app_detail::call_lora_api({"Info"}, &info);
        if (init_code == 0 && info_code == 0 && info.hw_ready)
            receive_code = lora_app_detail::call_lora_api({"StartReceive"});
    } catch (...) {
        init_code = -1;
    }

    if (init_code != 0 && info.diag[0] == '\0')
        std::snprintf(info.diag, sizeof(info.diag), "LoRa initialization failed (rc=%d)", init_code);
    else if (info_code != 0 && info.diag[0] == '\0')
        std::snprintf(info.diag, sizeof(info.diag), "LoRa initialization response unavailable (rc=%d)", info_code);
    if (receive_code != 0 && info.hw_ready)
        std::snprintf(info.diag, sizeof(info.diag), "LoRa receive mode unavailable (rc=%d)", receive_code);

    std::lock_guard<std::mutex> lock(state->mutex);
    state->init_code    = init_code;
    state->info_code    = info_code;
    state->receive_code = receive_code;
    state->info         = info;
    state->done         = true;
}

}  // namespace

UILoraPage::UILoraPage() : AppPage()
{
    set_page_title("LoRa");
    create_ui();
    if (!ui_ready()) {
        if (page_root_) lv_obj_delete(page_root_);
        return;
    }
    bind_events();
    init_lora();
}

UILoraPage::~UILoraPage()
{
    if (root_screen_) lv_obj_remove_event_cb_with_user_data(root_screen_, UILoraPage::static_key_event_cb, this);
    cancel_view_animations();
    cancel_message_title_animation();
    app_active_ = false;
    poll_timer_.stop();
    // Reap the async init thread before the page is gone. Joining here (rather
    // than detaching) guarantees the hardware init on the global backend state
    // is fully finished before the next page instance can touch it.
    if (init_thread_.joinable()) init_thread_.join();
    detach_delete_callbacks();
}

void UILoraPage::init_lora()
{
    if (!ui_ready()) return;
    app_active_               = true;
    scroll_to_latest_pending_ = false;
    lv_obj_clean(message_list_);
    last_message_row_ = nullptr;
    set_visible(empty_message_label_, true);
    set_visible(empty_message_hint_label_, true);
    lv_label_set_text(empty_message_hint_label_, "Initializing LoRa...");
    lv_obj_set_style_text_color(empty_message_hint_label_, lv_color_hex(0xC9A45C), LV_PART_MAIN | LV_STATE_DEFAULT);
    std::snprintf(lora_info_.diag, sizeof(lora_info_.diag), "Initializing LoRa hardware...");
    lora_info_.rx_event    = 0;
    lora_info_.tx_event    = 0;
    lora_info_.hw_ready    = 0;
    lora_info_.initialized = 0;
    model_.reset(false);
    render_current_view();
    poll_timer_.start(
        [this] { return lv_timer_create(&UILoraPage::static_poll_timer_cb, lora_app_detail::kPollIntervalMs, this); },
        [](lv_timer_t *timer) { lv_timer_delete(timer); });

    start_lora_initialization();
}

void UILoraPage::start_lora_initialization()
{
    // A previous attempt (successful or failed) must be fully reaped before a
    // new one starts, otherwise two init threads could race on the global
    // backend hardware state.
    if (init_thread_.joinable()) init_thread_.join();

    initialization_pending_ = true;
    initialization_state_   = std::make_shared<lora_app_detail::LoraInitializationState>();
    const auto state        = initialization_state_;
    try {
        init_thread_ = std::thread([state] { run_lora_initialization(state); });
    } catch (...) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->init_code    = -1;
        state->info_code    = -1;
        state->receive_code = -1;
        std::snprintf(state->info.diag, sizeof(state->info.diag), "Unable to start LoRa initialization");
        state->done = true;
    }
}

bool UILoraPage::consume_lora_initialization()
{
    if (!initialization_pending_ || !initialization_state_) return false;

    cp0_lora_info_t info{};
    {
        std::lock_guard<std::mutex> lock(initialization_state_->mutex);
        if (!initialization_state_->done) return false;
        info = initialization_state_->info;
    }

    initialization_pending_ = false;
    lora_info_              = info;
    lora_info_.rx_event     = 0;
    lora_info_.tx_event     = 0;
    model_.reset(lora_info_.hw_ready != 0);
    if (lora_info_.hw_ready) {
        lv_label_set_text(empty_message_hint_label_, "Type anything to send");
        lv_obj_set_style_text_color(empty_message_hint_label_, lv_color_hex(0x5FE492), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_label_set_text(empty_message_hint_label_, "LoRa unavailable; see Info");
        lv_obj_set_style_text_color(empty_message_hint_label_, lv_color_hex(0xD96C6C), LV_PART_MAIN | LV_STATE_DEFAULT);
        last_init_attempt_tick_ = lv_tick_get();
    }
    render_current_view();
    if (lora_info_.hw_ready) schedule_message_title_dismissal();
    return true;
}

bool UILoraPage::refresh_lora_info(bool poll)
{
    return lora_app_detail::call_lora_api({poll ? "Poll" : "Info"}, &lora_info_) == 0;
}

void UILoraPage::append_chat_message(const char *text, bool outgoing, float rssi, float snr)
{
    if (!message_list_) return;
    // A new bubble should never sit underneath the temporary Messages HUD.
    // The title is only an entry hint, so dismiss it as soon as real content
    // arrives instead of waiting for its timer.
    dismiss_message_title();
    const bool history_full = model_.messages().size() >= LoraPageModel::MESSAGE_HISTORY_LIMIT;
    if (history_full) {
        lv_obj_t *oldest_row = lv_obj_get_child(message_list_, 0);
        if (oldest_row) lv_obj_delete(oldest_row);
    }
    model_.append_message(text ? text : "", outgoing, rssi, snr);
    last_message_row_ = append_message_row(model_.messages().back());
    set_visible(empty_message_label_, false);
    set_visible(empty_message_hint_label_, false);
    if (model_.view() == LoraView::MESSAGES)
        scroll_to_latest(LV_ANIM_ON);
    else
        scroll_to_latest_pending_ = true;
}

void UILoraPage::open_send_view(uint32_t first_key)
{
    model_.begin_send(lora_app_detail::key_to_ascii(first_key));
    render_current_view();
}

void UILoraPage::scroll_messages(int32_t amount)
{
    dismiss_message_title();
    if (message_list_) lv_obj_scroll_by_bounded(message_list_, 0, amount, LV_ANIM_ON);
}

void UILoraPage::cancel_send()
{
    model_.cancel_send();
    render_current_view();
}

bool UILoraPage::handle_send_key(uint32_t key)
{
    if (key == LV_KEY_ESC) {
        cancel_send();
    } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        model_.erase_character();
        update_send_content();
    } else if (key == LV_KEY_ENTER) {
        send_current_text();
    } else if (lora_app_detail::is_printable_ascii(key)) {
        append_text_key(key);
        update_send_content();
    }
    return true;
}

bool UILoraPage::handle_navigation_key(uint32_t key)
{
    if (lora_app_detail::is_menu_prev_key(key)) {
        model_.set_view(LoraView::MESSAGES);
        render_current_view();
        return true;
    }
    if (lora_app_detail::is_menu_next_key(key)) {
        model_.set_view(LoraView::INFO);
        render_current_view();
        return true;
    }
    if (model_.view() == LoraView::MESSAGES && (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
        scroll_messages(key == LV_KEY_UP ? lora_app_detail::kMessageScrollStep : -lora_app_detail::kMessageScrollStep);
        return true;
    }
    if (model_.view() != LoraView::MESSAGES && (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
        model_.set_view(key == LV_KEY_UP ? LoraView::MESSAGES : LoraView::INFO);
        render_current_view();
        return true;
    }
    if (key == LV_KEY_ENTER) {
        if (initialization_pending_ || !lora_info_.hw_ready) return true;
        open_send_view(0);
        return true;
    }
    if (initialization_pending_ || !lora_info_.hw_ready) return lora_app_detail::is_printable_ascii(key);
    if (lora_app_detail::is_printable_ascii(key) && key != 'z' && key != 'Z' && key != 'c' && key != 'C') {
        open_send_view(key);
        return true;
    }
    return false;
}

bool UILoraPage::handle_key(uint32_t key)
{
    if (model_.view() == LoraView::SEND) return handle_send_key(key);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        if (navigate_home) navigate_home();
        return true;
    }
    return handle_navigation_key(key);
}

void UILoraPage::append_text_key(uint32_t key)
{
    model_.append_character(lora_app_detail::key_to_ascii(key));
}

void UILoraPage::send_current_text()
{
    if (initialization_pending_) {
        model_.set_send_status("LoRa is still initializing");
        update_send_content();
        return;
    }
    if (!lora_info_.hw_ready) {
        model_.set_send_status("LoRa unavailable");
        update_send_content();
        return;
    }
    if (model_.tx_input().empty()) {
        model_.set_send_status("Message is empty :(");
        update_send_content();
        return;
    }
    std::string sent_text(model_.tx_input());
    if (lora_app_detail::call_lora_api({"SendText", sent_text}) == 0) {
        refresh_lora_info(false);
        append_chat_message(sent_text.c_str(), true, 0.0f, 0.0f);
        model_.complete_send();
        render_current_view();
    } else {
        model_.set_send_status("Send failed");
        update_send_content();
    }
}

uint32_t UILoraPage::normalize_key(const key_item *key_event) const
{
    if (!key_event || lora_app_detail::is_modifier_event(key_event)) return 0;

    const uint32_t key = key_event->key_code;

    // Handle physical navigation keys before consulting text metadata. This
    // keeps a stale/incorrect codepoint on Enter or an arrow from becoming a
    // chat character.
    if (key == KEY_UP) return LV_KEY_UP;
    if (key == KEY_DOWN) return LV_KEY_DOWN;
    if (key == KEY_LEFT) return LV_KEY_LEFT;
    if (key == KEY_RIGHT) return LV_KEY_RIGHT;
    if (key == KEY_ENTER || key == KEY_KPENTER) return LV_KEY_ENTER;
    if (key == KEY_ESC) return LV_KEY_ESC;
    if (key == KEY_BACKSPACE) return LV_KEY_BACKSPACE;
    if (key == KEY_DELETE) return LV_KEY_DEL;

    if (model_.view() != LoraView::SEND) {
        if (key == KEY_F) return LV_KEY_UP;
        if (key == KEY_X) return LV_KEY_DOWN;
    }

    return lora_app_detail::printable_text_key(key_event);
}

void UILoraPage::on_key_event(lv_event_t *event)
{
    auto *key_event = static_cast<key_item *>(lv_event_get_param(event));
    if (!key_event || key_event->key_state == KBD_KEY_RELEASED) return;
    const uint32_t key = normalize_key(key_event);
    if (key != 0) (void)handle_key(key);
}

void UILoraPage::on_poll_timer()
{
    if (!app_active_ || !page_root_) return;
    if (initialization_pending_) {
        (void)consume_lora_initialization();
        return;
    }
    if (!lora_info_.hw_ready) {
        // A transient init failure (cold boot, EXT5V rail still ramping, etc.)
        // must not wedge the page permanently: retry with a backoff so the
        // radio can come up on its own without hammering the backend.
        const uint32_t now = lv_tick_get();
        if (now - last_init_attempt_tick_ >= lora_app_detail::kInitRetryIntervalMs) {
            start_lora_initialization();
        }
        return;
    }
    if (!refresh_lora_info(true)) return;
    if (lora_info_.rx_event) append_chat_message(lora_info_.last_rx, false, lora_info_.rssi, lora_info_.snr);
    if (model_.view() == LoraView::INFO) update_info_content();
}

void UILoraPage::static_cancel_button_cb(lv_event_t *event) noexcept
{
    UILoraPage *self = nullptr;
    try {
        if (!event) return;
        self = static_cast<UILoraPage *>(lv_event_get_user_data(event));
        if (!self || !lora_send_action_callback_allowed(lv_event_get_current_target(event), self->send_cancel_button_,
                                                        self->app_active_, self->model_.view() == LoraView::SEND))
            return;
        self->cancel_send();
    } catch (...) {
        if (self) self->app_active_ = false;
    }
}

void UILoraPage::static_send_button_cb(lv_event_t *event) noexcept
{
    UILoraPage *self = nullptr;
    try {
        if (!event) return;
        self = static_cast<UILoraPage *>(lv_event_get_user_data(event));
        if (!self || !lora_send_action_callback_allowed(lv_event_get_current_target(event), self->send_confirm_button_,
                                                        self->app_active_, self->model_.view() == LoraView::SEND))
            return;
        self->send_current_text();
    } catch (...) {
        if (self) self->app_active_ = false;
    }
}

void UILoraPage::static_key_event_cb(lv_event_t *event) noexcept
{
    UILoraPage *self = nullptr;
    try {
        if (!event) return;
        self = static_cast<UILoraPage *>(lv_event_get_user_data(event));
        if (lv_event_get_code(event) == LV_EVENT_DELETE &&
            lv_event_get_target(event) == lv_event_get_current_target(event)) {
            if (self && lv_event_get_current_target(event) == self->root_screen_) {
                self->app_active_ = false;
                self->poll_timer_.stop();
                self->cancel_message_title_animation();
                self->cancel_view_animations();
            }
            return;
        }
        if (lv_event_get_code(event) != static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD) || !self ||
            !lora_page_event_callback_allowed(lv_event_get_current_target(event), self->root_screen_,
                                              self->app_active_))
            return;
        self->on_key_event(event);
    } catch (...) {
        if (self) self->app_active_ = false;
    }
}

void UILoraPage::static_poll_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = static_cast<UILoraPage *>(lv_timer_get_user_data(timer));
    if (!self) return;
    try {
        if (lora_poll_callback_allowed(self->poll_timer_.current(timer), self->app_active_)) self->on_poll_timer();
    } catch (...) {
        self->app_active_ = false;
    }
}

void UILoraPage::static_message_title_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = timer ? static_cast<UILoraPage *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!self || self->message_title_timer_ != timer) return;
    self->message_title_timer_ = nullptr;
    try {
        self->dismiss_message_title();
    } catch (...) {
        self->app_active_ = false;
    }
}
