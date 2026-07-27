#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

namespace setting {

void Developer::enter_pair_view(UISetupPage &page, bool enable_after_pair)
{
    model_.begin_pairing(enable_after_pair);
    SetupPageAccess access(page);
    access.set_view(SetupViewState::ADB_PAIR);
    lv_obj_t *cont = access.content_container();
    if (!cont) {
        model_.clear_pairing();
        access.set_view(SetupViewState::SUB);
        return;
    }
    lv_obj_clean(cont);
    pair_input_label_ = nullptr;
    pair_hint_label_ = nullptr;
    DeveloperPairViewBuildContract build;
    auto rollback = [&]() {
        lv_obj_clean(cont);
        pair_input_label_ = nullptr;
        pair_hint_label_ = nullptr;
        model_.clear_pairing();
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
    };

    if (!guide_label(cont, 8, 5, "Pair ADB host", 0xFFFFFF,
                     launcher_fonts().get("Montserrat-Bold.ttf", 14,
                                          LV_FREETYPE_FONT_STYLE_BOLD))) {
        rollback();
        return;
    }
    build.label_created();
    if (!guide_label(cont, 8, 28, "Paste the host's adbkey.pub:", 0xAAAAAA,
                     &lv_font_montserrat_10)) {
        rollback();
        return;
    }
    build.label_created();
    pair_input_label_ = guide_label(cont, 8, 48, "_", 0xFFFFFF, &lv_font_montserrat_10);
    if (!pair_input_label_) {
        rollback();
        return;
    }
    build.label_created();
    lv_obj_add_event_cb(pair_input_label_, view_object_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_width(pair_input_label_, 304);
    lv_label_set_long_mode(pair_input_label_, LV_LABEL_LONG_CLIP);
    pair_hint_label_ = guide_label(cont, 8, access.content_height() - 14,
                                   "OK: authorize   ESC: cancel", 0x777777,
                                   &lv_font_montserrat_10);
    if (!pair_hint_label_) {
        rollback();
        return;
    }
    build.label_created();
    lv_obj_add_event_cb(pair_hint_label_, view_object_delete_cb, LV_EVENT_DELETE, this);
    if (!build.ready()) rollback();
}

void Developer::enter_revoke_view(UISetupPage &page)
{
    refresh_adb_status(page);
    model_.reset_authorization_selection(authorizations_.size());
    SetupPageAccess(page).set_view(SetupViewState::ADB_AUTHORIZATIONS);
    build_authorizations_view(page);
}

void Developer::build_authorizations_view(UISetupPage &page)
{
    SetupPageAccess access(page);
    lv_obj_t *cont = access.content_container();
    if (!cont) return;
    lv_obj_clean(cont);
    guide_label(cont, 8, 3, "Authorized ADB hosts", 0xFFFFFF,
                launcher_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD));
    if (authorizations_.empty()) {
        guide_label(cont, 8, 48, "No authorized hosts", 0x888888, &lv_font_montserrat_12);
    } else {
        const std::size_t selected = model_.authorization_selection();
        const std::size_t first = model_.authorization_window_start();
        const std::size_t last = std::min(authorizations_.size(), first + 3);
        for (std::size_t i = first; i < last; ++i) {
            const int y = 27 + static_cast<int>(i - first) * 28;
            if (i == selected)
                guide_chip(cont, 4, y - 2, 312, 25, 0x2A2A2A, 0x444444, 2, 0);
            std::string label = authorizations_[i].label;
            if (label.size() > 25) label.resize(25);
            guide_label(cont, 10, y, label.c_str(),
                        i == selected ? 0xFFFFFF : 0xAAAAAA,
                        &lv_font_montserrat_10);
            std::string short_id = authorizations_[i].fingerprint.substr(0, 12);
            guide_label(cont, 218, y, short_id.c_str(), 0x666666, &lv_font_montserrat_10);
        }
    }
    guide_label(cont, 8, access.content_height() - 15,
                authorizations_.empty() ? "ESC: back" : "OK: revoke   ESC: back",
                0x777777, &lv_font_montserrat_10);
}

void Developer::handle_authorizations_key(UISetupPage &page, uint32_t key)
{
    if (key == KEY_ESC || key == KEY_LEFT) {
        SetupPageAccess access(page);
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
        return;
    }
    if (key == KEY_UP && model_.select_previous_authorization()) {
        build_authorizations_view(page);
    } else if (key == KEY_DOWN && model_.select_next_authorization(authorizations_.size())) {
        build_authorizations_view(page);
    } else if ((key == KEY_ENTER || key == KEY_RIGHT) && !authorizations_.empty()) {
        const std::string fingerprint =
            authorizations_[model_.authorization_selection()].fingerprint;
        UISetupPage *page_ptr = &page;
        SetupPageAccess(page).confirm("Revoke selected ADB host?", [this, page_ptr, fingerprint]() {
            run_admin_action(*page_ptr, {"AdbRevoke", fingerprint}, "Revoking ADB host",
                             "Removing this key...", false);
        });
    }
}

void Developer::handle_pair_key(UISetupPage &page, uint32_t key)
{
    if (key == KEY_ESC || key == KEY_LEFT) {
        model_.clear_pairing();
        SetupPageAccess access(page);
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
        return;
    }
    if (key == KEY_ENTER) {
        submit_pairing(page);
        return;
    }
    if (key == KEY_BACKSPACE) {
        model_.erase_pairing_character();
    } else {
        const std::string_view utf8 = SetupPageAccess(page).current_utf8();
        if (!utf8.empty()) model_.append_pairing_text(utf8);
    }
    if (pair_input_label_) {
        const std::string shown = model_.pairing_preview();
        lv_label_set_text(pair_input_label_, shown.c_str());
    }
}

void Developer::show_status(const char *title_text, const char *detail_text, Modal modal)
{
    close_status();
    lv_obj_t *layer = lv_layer_top();
    if (!layer) return;
    status_overlay_ = lv_obj_create(layer);
    if (!status_overlay_) return;
    overlay_lifecycle_.open();
    lv_obj_add_event_cb(status_overlay_, status_overlay_delete_cb, LV_EVENT_DELETE, this);
    modal_ = modal;
    lv_obj_remove_style_all(status_overlay_);
    const auto layout = SetupPageAccess::fixed_layout();
    lv_obj_set_size(status_overlay_, layout.screen_width, layout.screen_height + 20);
    lv_obj_set_style_bg_color(status_overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(status_overlay_, LV_OPA_60, 0);
    lv_obj_clear_flag(status_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *box = guide_chip(status_overlay_, 35, 69, 250, 82, 0x1A1A2E,
                               modal == Modal::ERROR ? 0xCC5555 : 0x3A5A8A, 6, 1);
    if (!box) {
        close_status();
        return;
    }
    lv_obj_t *title = guide_label(
        box, 10, 10, title_text, 0xFFFFFF,
        launcher_fonts().get("Montserrat-Bold.ttf", 14, LV_FREETYPE_FONT_STYLE_BOLD));
    if (!title) {
        close_status();
        return;
    }
    lv_obj_set_width(title, 230);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *detail = guide_label(box, 10, 39, detail_text, 0xAAAAAA, &lv_font_montserrat_10);
    if (!detail) {
        close_status();
        return;
    }
    lv_obj_set_width(detail, 230);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    if (modal == Modal::ERROR) {
        lv_obj_t *hint = guide_label(box, 10, 64, "OK / ESC: close", 0x777777,
                                     &lv_font_montserrat_10);
        if (!hint) {
            close_status();
            return;
        }
        lv_obj_set_width(hint, 230);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_move_foreground(status_overlay_);
}

void Developer::close_status()
{
    if (status_overlay_) {
        lv_obj_t *overlay = status_overlay_;
        status_overlay_ = nullptr;
        lv_obj_remove_event_cb_with_user_data(
            overlay, status_overlay_delete_cb, this);
        lv_obj_del(overlay);
    }
    modal_ = Modal::NONE;
    overlay_lifecycle_.close();
}

void Developer::status_overlay_delete_cb(lv_event_t *event) noexcept
{
    try {
        auto *self = static_cast<Developer *>(lv_event_get_user_data(event));
        auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (!self || !setting::developer_delete_callback_allowed(
                deleted, lv_event_get_current_target(event), self->status_overlay_))
            return;
        self->status_overlay_ = nullptr;
        self->modal_ = Modal::NONE;
        self->overlay_lifecycle_.close();
    } catch (...) {
    }
}

void Developer::view_object_delete_cb(lv_event_t *event) noexcept
{
    try {
        auto *self = static_cast<Developer *>(lv_event_get_user_data(event));
        auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        auto *current = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
        if (!self || !deleted || deleted != current) return;
        if (setting::developer_delete_callback_allowed(
                deleted, current, self->usb_guide_knob_))
            self->usb_guide_knob_ = nullptr;
        if (setting::developer_delete_callback_allowed(
                deleted, current, self->pair_input_label_))
            self->pair_input_label_ = nullptr;
        if (setting::developer_delete_callback_allowed(
                deleted, current, self->pair_hint_label_))
            self->pair_hint_label_ = nullptr;
    } catch (...) {
    }
}

void Developer::handle_status_key(UISetupPage &page, uint32_t key)
{
    if (modal_ == Modal::BUSY) return;
    if (key == KEY_ENTER || key == KEY_ESC || key == KEY_LEFT) {
        close_status();
        SetupPageAccess(page).build_sub_view();
    }
}

void Developer::enter_usb_guide(UISetupPage &page, bool enabling)
{
    usb_guide_enabling_ = enabling;
    build_usb_guide_view(page);
}

lv_obj_t *Developer::guide_chip(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t bg, uint32_t border, int radius, int border_w)
{
    if (!parent) return nullptr;
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) return nullptr;
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, lv_color_hex(border), LV_PART_MAIN);
    lv_obj_set_style_border_width(o, border_w, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *Developer::guide_label(lv_obj_t *parent, int x, int y, const char *txt,
                                 uint32_t color, const lv_font_t *font)
{
    if (!parent) return nullptr;
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_label_set_text(label, txt);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    return label;
}

void Developer::build_usb_guide_view(UISetupPage &page)
{
    SetupPageAccess access(page);
    access.set_view(SetupViewState::USB_GUIDE);
    stop_usb_guide_anims();
    lv_obj_t *cont = access.content_container();
    if (!cont) return;
    lv_obj_clean(cont);
    const bool en = usb_guide_enabling_;
    const uint32_t C_GREEN = 0x46DC87, C_YEL = 0xF0C850, C_RED = 0xEB5F5F;
    const uint32_t C_WHITE = 0xECECEC, C_GREY = 0x9A9AA0;
    const lv_font_t *f_title =
        launcher_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD);
    const lv_font_t *f_msg = &lv_font_montserrat_10;

    guide_label(cont, 8, 2,
                en ? "Enable ADB - switch USB to device" : "Disable ADB - switch USB to HUB",
                C_WHITE, f_title ? f_title : &lv_font_montserrat_12);
    guide_chip(cont, 86, 24, 146, 50, 0x282A30, 0x5A5C64, 6, 2);
    guide_label(cont, 120, 28, "CardputerZero", C_GREY, f_msg);
    guide_chip(cont, 218, 30, 12, 12, 0x101012, 0x5A5C64, 3, 2);
    guide_chip(cont, 228, 32, 22, 8, 0xCDCDD2, 0xCDCDD2, 2, 0);
    guide_chip(cont, 250, 34, 60, 4, 0x6A6C72, 0x6A6C72, 2, 0);
    guide_label(cont, 232, 42, "USB-C", C_GREEN, f_msg);
    guide_chip(cont, 24, 28, 32, 44, 0x1A1A1C, 0x5A5C64, 6, 2);
    guide_chip(cont, 33, 33, 14, 34, 0x0E0E10, 0x0E0E10, 4, 0);
    guide_label(cont, 26, 14, "USB", en ? C_GREEN : C_GREY, f_msg);
    guide_label(cont, 28, 72, "HUB", en ? C_RED : C_GREEN, f_msg);
    const int thumb_top = 34, thumb_bot = 54;
    usb_guide_knob_ =
        guide_chip(cont, 32, en ? thumb_bot : thumb_top, 16, 10, C_GREEN, 0x2A6F49, 3, 1);
    if (usb_guide_knob_)
        lv_obj_add_event_cb(usb_guide_knob_, view_object_delete_cb, LV_EVENT_DELETE, this);

    const int y = 80;
    if (en) {
        guide_label(cont, 8, y, "1  Slide LEFT switch  HUB -> USB", C_WHITE, f_msg);
        guide_label(cont, 8, y + 15, "2  USB hub & peripherals turn OFF", C_YEL, f_msg);
        guide_label(cont, 8, y + 30, "3  Cable -> top-right USB-C port", C_GREEN, f_msg);
    } else {
        guide_label(cont, 8, y, "1  Slide LEFT switch  USB -> HUB", C_WHITE, f_msg);
        guide_label(cont, 8, y + 15, "2  USB hub & peripherals come back", C_GREEN, f_msg);
        guide_label(cont, 8, y + 30, "3  Reboot to apply the change", C_YEL, f_msg);
    }
    guide_label(cont, 8, access.content_height() - 16, "OK: reboot now     ESC: later", C_GREY,
                &lv_font_montserrat_10);

    if (!usb_guide_knob_) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, usb_guide_knob_);
    lv_anim_set_values(&animation, en ? thumb_bot : thumb_top, en ? thumb_top : thumb_bot);
    lv_anim_set_time(&animation, 650);
    lv_anim_set_playback_time(&animation, 650);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&animation,
                        [](void *var, int32_t value) { lv_obj_set_y((lv_obj_t *)var, value); });
    lv_anim_start(&animation);
}

void Developer::stop_usb_guide_anims()
{
    if (usb_guide_knob_) {
        lv_obj_t *knob = usb_guide_knob_;
        usb_guide_knob_ = nullptr;
        lv_anim_del(knob, nullptr);
        lv_obj_remove_event_cb_with_user_data(
            knob, view_object_delete_cb, this);
    }
}

void Developer::handle_usb_guide_key(UISetupPage &page, uint32_t key)
{
    switch (key) {
    case KEY_ENTER:
    case KEY_RIGHT: {
        stop_usb_guide_anims();
        lv_obj_t *cont = SetupPageAccess(page).content_container();
        if (!cont) break;
        lv_obj_clean(cont);
        lv_obj_t *label = lv_label_create(cont);
        if (!label) break;
        lv_label_set_text(label, "Rebooting...");
        lv_obj_center(label);
        cp0_signal_process_api({"Reboot"}, nullptr);
        break;
    }
    case KEY_ESC:
    case KEY_LEFT: {
        stop_usb_guide_anims();
        SetupPageAccess access(page);
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
        break;
    }
    default:
        break;
    }
}

} // namespace setting
