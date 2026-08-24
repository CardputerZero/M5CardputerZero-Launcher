#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

namespace setting {

namespace {

lv_obj_t *create_modal_overlay(int width, int height)
{
    lv_obj_t *layer = lv_layer_top();
    if (!layer) return nullptr;
    lv_obj_t *overlay = lv_obj_create(layer);
    if (!overlay) return nullptr;
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, width, height);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    return overlay;
}

lv_obj_t *create_modal_box(lv_obj_t *overlay, int width, int height, uint32_t border_color)
{
    if (!overlay) return nullptr;
    lv_obj_t *box = lv_obj_create(overlay);
    if (!box) return nullptr;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, width, height);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    return box;
}

} // namespace

void RTC::show_status(const char *title_text, const char *detail_text, Modal modal)
{
    const auto layout = SetupPageAccess::fixed_layout();
    lv_obj_t *overlay = create_modal_overlay(layout.screen_width, layout.screen_height + 20);
    if (!overlay) return;
    lv_obj_t *box = create_modal_box(overlay, 250, 82,
                                     modal == Modal::ERROR ? 0xCC5555 : 0x3A5A8A);
    if (!box) {
        lv_obj_del(overlay);
        return;
    }

    lv_obj_t *title = lv_label_create(box);
    if (!title) {
        lv_obj_del(overlay);
        return;
    }
    lv_label_set_text(title, title_text);
    lv_obj_set_width(title, 230);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(
        title,
        launcher_fonts().get("Montserrat-Bold.ttf", 14, LV_FREETYPE_FONT_STYLE_BOLD), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *detail = lv_label_create(box);
    if (!detail) {
        lv_obj_del(overlay);
        return;
    }
    lv_label_set_text(detail, detail_text);
    lv_obj_set_width(detail, 230);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_10, 0);
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, 7);
    if (modal == Modal::ERROR) {
        lv_obj_t *hint = lv_label_create(box);
        if (!hint) {
            lv_obj_del(overlay);
            return;
        }
        lv_label_set_text(hint, "OK / ESC: close");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x777777), 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
    }
    close_write_confirm();
    overlay_token_ = overlay_lifecycle_.open();
    if (!overlay_token_) {
        lv_obj_del(overlay);
        return;
    }
    confirm_overlay_ = overlay;
    modal_ = modal;
    lv_obj_add_event_cb(confirm_overlay_, overlay_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_move_foreground(confirm_overlay_);
}

void RTC::show_write_confirm(UISetupPage &page)
{
    (void)page;
    if (confirm_overlay_) return;

    const auto layout = SetupPageAccess(page).layout();
    lv_obj_t *overlay = create_modal_overlay(layout.screen_width, layout.screen_height + 20);
    if (!overlay) return;
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *box = create_modal_box(overlay, 230, 86, 0x3A5A8A);
    if (!box) {
        lv_obj_del(overlay);
        return;
    }

    lv_obj_t *title = lv_label_create(box);
    if (!title) {
        lv_obj_del(overlay);
        return;
    }
    lv_label_set_text(title, "Write hardware RTC?");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(
        title,
        launcher_fonts().get("Montserrat-Bold.ttf", 14, LV_FREETYPE_FONT_STYLE_BOLD), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *hint = lv_label_create(box);
    if (!hint) {
        lv_obj_del(overlay);
        return;
    }
    lv_label_set_text(hint, "OK:confirm  ESC:no");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -7);

    lv_obj_t *yes_label = lv_label_create(box);
    if (!yes_label) {
        lv_obj_del(overlay);
        return;
    }
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_font(
        yes_label,
        launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD), 0);
    lv_obj_align(yes_label, LV_ALIGN_CENTER, -42, 8);

    lv_obj_t *no_label = lv_label_create(box);
    if (!no_label) {
        lv_obj_del(overlay);
        return;
    }
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_font(
        no_label,
        launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD), 0);
    lv_obj_align(no_label, LV_ALIGN_CENTER, 42, 8);

    overlay_token_ = overlay_lifecycle_.open();
    if (!overlay_token_) {
        lv_obj_del(overlay);
        return;
    }
    confirm_overlay_ = overlay;
    confirm_yes_lbl_ = yes_label;
    confirm_no_lbl_ = no_label;
    modal_ = Modal::CONFIRM;
    confirm_model_.reset();
    lv_obj_add_event_cb(confirm_overlay_, overlay_delete_cb, LV_EVENT_DELETE, this);
    update_write_confirm_buttons();
    lv_obj_move_foreground(confirm_overlay_);
    lv_refr_now(nullptr);
}

void RTC::close_write_confirm()
{
    if (confirm_overlay_) {
        lv_obj_t *overlay = confirm_overlay_;
        confirm_overlay_ = nullptr;
        lv_obj_remove_event_cb_with_user_data(overlay, overlay_delete_cb, this);
        lv_obj_del(overlay);
    }
    confirm_yes_lbl_ = nullptr;
    confirm_no_lbl_ = nullptr;
    modal_ = Modal::NONE;
    overlay_lifecycle_.close(overlay_token_);
    overlay_token_ = 0;
}

void RTC::overlay_delete_cb(lv_event_t *event) noexcept
{
    try {
        if (!event) return;
        auto *self = static_cast<RTC *>(lv_event_get_user_data(event));
        auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (!self || !setting::rtc_overlay_delete_callback_allowed(
                deleted, lv_event_get_current_target(event), self->confirm_overlay_))
            return;
        self->confirm_overlay_ = nullptr;
        self->confirm_yes_lbl_ = nullptr;
        self->confirm_no_lbl_ = nullptr;
        self->modal_ = Modal::NONE;
        self->overlay_lifecycle_.close(self->overlay_token_);
        self->overlay_token_ = 0;
    } catch (...) {
    }
}

void RTC::update_write_confirm_buttons()
{
    if (!confirm_yes_lbl_ || !confirm_no_lbl_) return;
    lv_obj_set_style_text_color(confirm_yes_lbl_,
                                lv_color_hex(confirm_model_.save_selected() ? 0x00CC66 : 0x888888), 0);
    lv_obj_set_style_text_color(confirm_no_lbl_,
                                lv_color_hex(confirm_model_.save_selected() ? 0x888888 : 0x00CC66), 0);
}

void RTC::handle_write_confirm_key(UISetupPage &page, uint32_t key)
{
    if (modal_ == Modal::BUSY) return;
    if (modal_ == Modal::ERROR) {
        if (key == KEY_ENTER || key == KEY_ESC) {
            SetupPageAccess access(page);
            access.play_back();
            close_write_confirm();
            if (access.is_view(SetupViewState::SUB)) access.build_sub_view();
        }
        return;
    }
    switch (key) {
    case KEY_LEFT:
    case KEY_UP:
        confirm_model_.handle(RtcConfirmInput::SELECT_SAVE);
        update_write_confirm_buttons();
        break;
    case KEY_RIGHT:
    case KEY_DOWN:
        confirm_model_.handle(RtcConfirmInput::SELECT_DISCARD);
        update_write_confirm_buttons();
        break;
    case KEY_ENTER:
        SetupPageAccess(page).play_enter();
        close_write_confirm();
        if (confirm_model_.handle(RtcConfirmInput::CONFIRM) == RtcConfirmAction::SAVE) {
            commit_to_hardware(page);
        } else {
            refresh_values(page);
            SetupPageAccess access(page);
            access.set_view(SetupViewState::MAIN);
            access.build_main_view();
        }
        break;
    case KEY_ESC: {
        SetupPageAccess(page).play_back();
        close_write_confirm();
        confirm_model_.handle(RtcConfirmInput::CANCEL);
        refresh_values(page);
        SetupPageAccess access(page);
        access.set_view(SetupViewState::MAIN);
        access.build_main_view();
        break;
    }
    default:
        break;
    }
}

} // namespace setting
