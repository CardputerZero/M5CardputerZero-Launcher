#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

namespace setting {

SoundCard::~SoundCard()
{
    shutdown();
}

void SoundCard::shutdown()
{
    cursor_stop();
    stop_transition_timer();
    clear_detail_view();
}

void SoundCard::enter_cards(UISetupPage &page)
{
    stop_transition_timer();
    clear_detail_view();
    std::vector<SoundCardInfo> candidate;
    bool loaded = false;
    try {
        cp0_signal_soundcard_api({"ListCards"}, [&](int code, std::string data) {
            if (code != 0) return;
            candidate = SoundCardModel::parse_cards(data);
            loaded = true;
        });
    } catch (...) {
        loaded = false;
    }
    if (!sound_card_commit_if_success(loaded, std::move(candidate), cards_)) return;
    card_sel_ = 0;
    SetupPageAccess access(page);
    access.set_view(SetupViewState::SOUNDCARD_CARDS);
    access.transition_enter();
}

void SoundCard::enter_controls(UISetupPage &page)
{
    stop_transition_timer();
    clear_detail_view();
    if (cards_.empty()) return;
    const int candidate_card_idx = cards_[card_sel_].index;
    std::vector<SoundControlInfo> candidate;
    bool loaded = false;
    try {
        cp0_signal_soundcard_api({"ListControls", std::to_string(candidate_card_idx)},
        [&](int code, std::string data) {
            if (code != 0) return;
            candidate = SoundCardModel::parse_controls(data);
            loaded = true;
        });
    } catch (...) {
        loaded = false;
    }
    if (!sound_card_commit_if_success(loaded, std::move(candidate), controls_)) return;
    card_idx_ = candidate_card_idx;
    ctrl_sel_ = 0;
    SetupPageAccess access(page);
    access.set_view(SetupViewState::SOUNDCARD_CONTROLS);
    access.transition_enter();
}

void SoundCard::enter_detail(UISetupPage &page)
{
    if (controls_.empty()) return;
    stop_transition_timer();
    clear_detail_view();
    const auto &ctrl = controls_[ctrl_sel_];
    SoundControlInfo candidate = ctrl;
    bool loaded = false;
    try {
        cp0_signal_soundcard_api({"GetControlDetail", std::to_string(card_idx_), ctrl.name},
        [&](int code, std::string data) {
            if (code != 0) return;
            candidate = SoundCardModel::parse_detail(data, ctrl);
            loaded = true;
        });
    } catch (...) {
        loaded = false;
    }
    if (!sound_card_commit_if_success(loaded, std::move(candidate), detail_)) return;
    input_buf_.clear();
    input_lbl_  = nullptr;
    hint_lbl_  = nullptr;
    SetupPageAccess access(page);
    access.set_view(SetupViewState::SOUNDCARD_DETAIL);
    access.transition_enter();
}

// ====================================================================
//  Build: card list view
// ====================================================================
// Apply the typed value via cp0_signal_soundcard_api
void SoundCard::apply_value(UISetupPage &page)
{
    if (input_buf_.empty()) return;

    const int new_val = SoundCardModel::clamp_value(
        SoundCardModel::parse_value(input_buf_, detail_.current_value), detail_);

    // Visual feedback while applying
    if (hint_lbl_) {
        lv_label_set_text(hint_lbl_, "Applying...");
        lv_obj_set_style_text_color(hint_lbl_, lv_color_hex(0xFFAA00), LV_PART_MAIN);
        lv_refr_now(NULL);
    }

    int rc = -1;
    sound_card_invoke_backend([&] {
        cp0_signal_soundcard_api(
            {"SetControl", std::to_string(card_idx_), detail_.name, std::to_string(new_val)},
            [&rc](int code, std::string) { rc = code; });
    });

    if (hint_lbl_) {
        if (rc == 0) {
            lv_label_set_text(hint_lbl_, "Applied OK");
            lv_obj_set_style_text_color(hint_lbl_, lv_color_hex(0x33CC33), LV_PART_MAIN);
        } else {
            lv_label_set_text(hint_lbl_, "Error (check amixer)");
            lv_obj_set_style_text_color(hint_lbl_, lv_color_hex(0xFF4444), LV_PART_MAIN);
        }
        lv_refr_now(NULL);
    }

    // Refresh the control list entry with the new value
    if (rc == 0 && ctrl_sel_ < (int)controls_.size()) {
        char val_str[32];
        std::snprintf(val_str, sizeof(val_str), "%d", new_val);
        controls_[ctrl_sel_].current_value = new_val;
        controls_[ctrl_sel_].current_text = val_str;
    }

    // Go back to control list after a short pause
    clear_detail_view();
    SetupPageAccess(page).set_view(SetupViewState::SOUNDCARD_CONTROLS);
    stop_transition_timer();
    if (!transition_model_.begin()) return;
    transition_page_ = &page;
    transition_timer_ = lv_timer_create(transition_timer_cb, 900, this);
    if (transition_timer_ && page.screen()) {
        lv_timer_set_repeat_count(transition_timer_, 1);
        lv_obj_add_event_cb(page.screen(), SoundCard::transition_screen_delete_cb,
                            LV_EVENT_DELETE, this);
    } else {
        if (transition_timer_) {
            lv_timer_delete(transition_timer_);
            transition_timer_ = nullptr;
        }
        transition_page_ = nullptr;
        if (transition_model_.complete() && page.screen() &&
            SetupPageAccess(page).is_view(SetupViewState::SOUNDCARD_CONTROLS))
            SetupPageAccess(page).transition_back();
    }
}

// ====================================================================
//  Key handlers
// ====================================================================
void SoundCard::handle_cards_key(UISetupPage &page, uint32_t key)
{
    int total = (int)cards_.size();
    switch (key) {
    case KEY_UP:
        if (card_sel_ > 0) { --card_sel_; build_cards_view(page); }
        break;
    case KEY_DOWN:
        if (card_sel_ < total - 1) { ++card_sel_; build_cards_view(page); }
        break;
    case KEY_ENTER:
    case KEY_RIGHT:
        if (total > 0) { SetupPageAccess(page).play_enter(); enter_controls(page); }
        break;
    case KEY_ESC:
    case KEY_LEFT:
        stop_transition_timer();
        clear_detail_view();
        SetupPageAccess(page).play_back();
        SetupPageAccess(page).set_view(SetupViewState::SUB);
        SetupPageAccess(page).transition_back();
        break;
    default:
        break;
    }
}

void SoundCard::handle_controls_key(UISetupPage &page, uint32_t key)
{
    int total = (int)controls_.size();
    switch (key) {
    case KEY_UP:
        if (ctrl_sel_ > 0) { --ctrl_sel_; build_controls_view(page); }
        break;
    case KEY_DOWN:
        if (ctrl_sel_ < total - 1) { ++ctrl_sel_; build_controls_view(page); }
        break;
    case KEY_ENTER:
    case KEY_RIGHT:
        if (total > 0) { SetupPageAccess(page).play_enter(); enter_detail(page); }
        break;
    case KEY_ESC:
    case KEY_LEFT:
        stop_transition_timer();
        clear_detail_view();
        SetupPageAccess(page).play_back();
        SetupPageAccess(page).set_view(SetupViewState::SOUNDCARD_CARDS);
        SetupPageAccess(page).transition_back();
        break;
    default:
        break;
    }
}

void SoundCard::handle_detail_key(UISetupPage &page, uint32_t key)
{
    // Digit keys: accumulate input
    if (key == KEY_0 || (key >= KEY_1 && key <= KEY_9)) {
        // KEY_1..KEY_9 map to '1'..'9', KEY_0 maps to '0'
        // Linux input key codes: KEY_1=2..KEY_9=10, KEY_0=11
        int digit = -1;
        if (key == KEY_0)         digit = 0;
        else if (key >= KEY_1 && key <= KEY_9) digit = (int)(key - KEY_1 + 1);
        if (digit >= 0 && input_buf_.size() < 8) {
            input_buf_ += (char)('0' + digit);
            input_update_display();
        }
        return;
    }

    switch (key) {
    case KEY_BACKSPACE:
        if (!input_buf_.empty()) {
            input_buf_.pop_back();
            input_update_display();
        }
        break;
    case KEY_ENTER:
    case KEY_RIGHT:
        apply_value(page);
        break;
    case KEY_ESC:
    case KEY_LEFT:
        clear_detail_view();
        SetupPageAccess(page).play_back();
        SetupPageAccess(page).set_view(SetupViewState::SOUNDCARD_CONTROLS);
        SetupPageAccess(page).transition_back();
        break;
    default:
        // Also accept typed digit characters forwarded by the input layer.
        const std::string_view utf8 = SetupPageAccess(page).current_utf8();
        if (!utf8.empty() && utf8[0] >= '0' && utf8[0] <= '9') {
            if (input_buf_.size() < 8) {
                input_buf_ += utf8[0];
                input_update_display();
            }
        }
        break;
    }
}

void SoundCard::clear_detail_view()
{
    cursor_stop();
    if (input_lbl_)
        lv_obj_remove_event_cb_with_user_data(
            input_lbl_, SoundCard::input_label_delete_cb, this);
    if (hint_lbl_)
        lv_obj_remove_event_cb_with_user_data(
            hint_lbl_, SoundCard::hint_label_delete_cb, this);
    input_lbl_ = nullptr;
    hint_lbl_ = nullptr;
}

void SoundCard::stop_transition_timer()
{
    if (transition_page_ && transition_page_->screen())
        lv_obj_remove_event_cb_with_user_data(
            transition_page_->screen(), SoundCard::transition_screen_delete_cb, this);
    if (transition_timer_) {
        lv_timer_del(transition_timer_);
        transition_timer_ = nullptr;
    }
    transition_page_ = nullptr;
    transition_model_.cancel();
}

void SoundCard::input_label_delete_cb(lv_event_t *event) noexcept
{
    if (!event) return;
    auto *self = static_cast<SoundCard *>(lv_event_get_user_data(event));
    if (!self || !sound_card_owned_label_delete_matches(
            lv_event_get_target(event), lv_event_get_current_target(event),
            self->input_lbl_)) return;
    self->input_lbl_ = nullptr;
    self->cursor_stop();
}

void SoundCard::hint_label_delete_cb(lv_event_t *event) noexcept
{
    if (!event) return;
    auto *self = static_cast<SoundCard *>(lv_event_get_user_data(event));
    if (!self || !sound_card_owned_label_delete_matches(
            lv_event_get_target(event), lv_event_get_current_target(event),
            self->hint_lbl_)) return;
    self->hint_lbl_ = nullptr;
}

void SoundCard::transition_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = static_cast<SoundCard *>(lv_timer_get_user_data(timer));
    if (!self || !sound_card_transition_timer_callback_allowed(
            timer, self->transition_timer_, self->transition_page_,
            self->transition_model_.pending())) return;
    try {
        UISetupPage *page = self->transition_page_;
        if (page->screen())
            lv_obj_remove_event_cb_with_user_data(
                page->screen(), transition_screen_delete_cb, self);
        self->transition_timer_ = nullptr;
        self->transition_page_ = nullptr;
        if (!self->transition_model_.complete()) return;
        if (SetupPageAccess(*page).is_view(SetupViewState::SOUNDCARD_CONTROLS))
            SetupPageAccess(*page).transition_back();
    } catch (...) {
        self->transition_timer_ = nullptr;
        self->transition_page_ = nullptr;
        self->transition_model_.cancel();
    }
}

void SoundCard::transition_screen_delete_cb(lv_event_t *event) noexcept
{
    if (!event) return;
    auto *self = static_cast<SoundCard *>(lv_event_get_user_data(event));
    if (!self || !self->transition_page_ ||
        !sound_card_transition_screen_delete_matches(
            lv_event_get_target(event), lv_event_get_current_target(event),
            self->transition_page_->screen())) return;
    self->transition_page_ = nullptr;
    if (self->transition_timer_) {
        lv_timer_del(self->transition_timer_);
        self->transition_timer_ = nullptr;
    }
    self->transition_model_.cancel();
}


void SoundCard::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    UISetupPage *page = &p;
    SoundCard *controller = this;
    MenuItem m;
    m.label = "SoundCard";
    m.sub_items = {{"Open Mixer", false, false, [page, controller]() { controller->enter_cards(*page); }}};
    menu.push_back(m);
}


} // namespace setting
