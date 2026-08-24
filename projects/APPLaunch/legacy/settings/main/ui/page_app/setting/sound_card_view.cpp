#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

namespace setting {

void SoundCard::build_cards_view(UISetupPage &page)
{
    clear_detail_view();
    SetupPageAccess access(page);
    const SetupLayout layout = access.layout();
    lv_obj_t *cont = access.content_container();
    if (!cont) return;
    lv_obj_clean(cont);

    // Title
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "Sound Cards");
    access.apply_fixed_label_box(title, layout.sc_margin_x, 4, layout.sc_detail_text_width, false);
    lv_obj_set_style_text_color(title, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, launcher_fonts().get("Montserrat-Bold.ttf", 14, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);

    if (cards_.empty()) {
        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "No ALSA cards found.");
        access.apply_fixed_label_box(lbl, layout.sc_margin_x, 40, layout.sc_detail_text_width, false);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);

        lv_obj_t *hint = lv_label_create(cont);
        lv_label_set_text(hint, "ESC: back");
        access.apply_fixed_label_box(hint, layout.sc_margin_x, layout.content_height - 14, layout.sc_bottom_hint_width, false);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), LV_PART_MAIN);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN);
        return;
    }

    int visible = 5;
    int total   = (int)cards_.size();
    int offset  = card_sel_ - visible / 2;
    if (offset < 0) offset = 0;
    if (total > visible && offset > total - visible) offset = total - visible;

    for (int vi = 0; vi < visible && (vi + offset) < total; ++vi) {
        int ai  = vi + offset;
        bool sel = (ai == card_sel_);
        int  y   = 22 + vi * 22;

        if (sel) {
            lv_obj_t *bg = lv_obj_create(cont);
            lv_obj_set_size(bg, layout.screen_width - 8, 20);
            lv_obj_set_pos(bg, 4, y);
            lv_obj_set_style_radius(bg, 2, LV_PART_MAIN);
            lv_obj_set_style_bg_color(bg, lv_color_hex(0x1F3A5F), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bg, 255, LV_PART_MAIN);
            lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
            lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
        }

        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, cards_[ai].name.c_str());
        access.apply_fixed_label_box(lbl, layout.sc_row_x, y + 2, layout.sc_card_name_width, sel);
        lv_obj_set_style_text_color(lbl, lv_color_hex(sel ? 0xFFFFFF : 0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    }

    lv_obj_t *hint = lv_label_create(cont);
    lv_label_set_text(hint, "OK: open  ESC: back");
    access.apply_fixed_label_box(hint, layout.sc_margin_x, layout.content_height - 14, layout.sc_bottom_hint_width, false);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN);
}

// ====================================================================
//  Build: control list view
// ====================================================================
void SoundCard::build_controls_view(UISetupPage &page)
{
    clear_detail_view();
    SetupPageAccess access(page);
    const SetupLayout layout = access.layout();
    lv_obj_t *cont = access.content_container();
    if (!cont) return;
    lv_obj_clean(cont);

    // Title: card name
    char title_buf[80];
    if (!cards_.empty() && card_sel_ < (int)cards_.size())
        std::snprintf(title_buf, sizeof(title_buf), "%s", cards_[card_sel_].name.c_str());
    else
        std::snprintf(title_buf, sizeof(title_buf), "Card %d", card_idx_);

    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, title_buf);
    access.apply_fixed_label_box(title, layout.sc_margin_x, 4, layout.sc_detail_text_width, true);
    lv_obj_set_style_text_color(title, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, launcher_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);

    if (controls_.empty()) {
        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "No controls found.");
        access.apply_fixed_label_box(lbl, layout.sc_margin_x, 40, layout.sc_detail_text_width, false);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);

        lv_obj_t *hint = lv_label_create(cont);
        lv_label_set_text(hint, "ESC: back");
        access.apply_fixed_label_box(hint, layout.sc_margin_x, layout.content_height - 14, layout.sc_bottom_hint_width, false);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), LV_PART_MAIN);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN);
        return;
    }

    int visible = 5;
    int total   = (int)controls_.size();
    int offset  = ctrl_sel_ - visible / 2;
    if (offset < 0) offset = 0;
    if (total > visible && offset > total - visible) offset = total - visible;

    for (int vi = 0; vi < visible && (vi + offset) < total; ++vi) {
        int ai  = vi + offset;
        bool sel = (ai == ctrl_sel_);
        int  y   = 20 + vi * 22;

        if (sel) {
            lv_obj_t *bg = lv_obj_create(cont);
            lv_obj_set_size(bg, layout.screen_width - 8, 20);
            lv_obj_set_pos(bg, 4, y);
            lv_obj_set_style_radius(bg, 2, LV_PART_MAIN);
            lv_obj_set_style_bg_color(bg, lv_color_hex(0x1F3A5F), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bg, 255, LV_PART_MAIN);
            lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
            lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
        }

        const auto &ctrl = controls_[ai];

        // Left: control name
        lv_obj_t *name_lbl = lv_label_create(cont);
        lv_label_set_text(name_lbl, ctrl.name.c_str());
        access.apply_fixed_label_box(name_lbl, layout.sc_control_name_x, y + 2, layout.sc_control_name_width, sel);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(sel ? 0xFFFFFF : 0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, LV_PART_MAIN);

        // Right: current value summary
        if (!ctrl.current_text.empty()) {
            lv_obj_t *val_lbl = lv_label_create(cont);
            lv_label_set_text(val_lbl, ctrl.current_text.c_str());
            access.apply_fixed_label_box(val_lbl, layout.sc_control_value_x, y + 2, layout.sc_control_value_width, sel);
            lv_obj_set_style_text_color(val_lbl, lv_color_hex(sel ? 0xAADDFF : 0x888888), LV_PART_MAIN);
            lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_10, LV_PART_MAIN);
        }
    }

    // Scroll arrows
    if (ctrl_sel_ > 0) {
        lv_obj_t *a = lv_img_create(cont);
        lv_img_set_src(a, access.arrow_up_path().c_str());
        lv_obj_set_pos(a, layout.screen_width / 2 - 8, 2);
    }
    if (ctrl_sel_ < total - 1) {
        lv_obj_t *a = lv_img_create(cont);
        lv_img_set_src(a, access.arrow_down_path().c_str());
        lv_obj_set_pos(a, layout.screen_width / 2 - 8, layout.content_height - 28);
    }

    lv_obj_t *hint = lv_label_create(cont);
    lv_label_set_text(hint, "OK: edit  ESC: back");
    access.apply_fixed_label_box(hint, layout.sc_margin_x, layout.content_height - 14, layout.sc_bottom_hint_width, false);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN);
}

// ====================================================================
//  Build: detail / input view
// ====================================================================
void SoundCard::build_detail_view(UISetupPage &page)
{
    clear_detail_view();
    SetupPageAccess access(page);
    const SetupLayout layout = access.layout();
    lv_obj_t *cont = access.content_container();
    if (!cont) return;
    lv_obj_clean(cont);

    // Control name (header)
    lv_obj_t *name_lbl = lv_label_create(cont);
    lv_label_set_text(name_lbl, detail_.name.c_str());
    access.apply_fixed_label_box(name_lbl, layout.sc_margin_x, 4, layout.sc_detail_text_width, true);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(name_lbl, launcher_fonts().get("Montserrat-Bold.ttf", 14, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);

    // Info block: Limits + current value
    char info_buf[128];
    std::snprintf(info_buf, sizeof(info_buf),
                  "Limits: %d - %d", detail_.min_value, detail_.max_value);
    lv_obj_t *limits_lbl = lv_label_create(cont);
    lv_label_set_text(limits_lbl, info_buf);
    access.apply_fixed_label_box(limits_lbl, layout.sc_margin_x, 26, layout.sc_detail_text_width, false);
    lv_obj_set_style_text_color(limits_lbl, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(limits_lbl, &lv_font_montserrat_12, LV_PART_MAIN);

    if (!detail_.current_text.empty()) {
        lv_obj_t *cur_lbl = lv_label_create(cont);
        lv_label_set_text(cur_lbl, detail_.current_text.c_str());
        access.apply_fixed_label_box(cur_lbl, layout.sc_margin_x, 44, layout.sc_detail_text_width, true);
        lv_obj_set_style_text_color(cur_lbl, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(cur_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    }

    // Separator line
    lv_obj_t *sep = lv_obj_create(cont);
    lv_obj_set_size(sep, layout.screen_width - 16, 1);
    lv_obj_set_pos(sep, 8, 64);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // "New value:" label
    lv_obj_t *new_lbl = lv_label_create(cont);
    lv_label_set_text(new_lbl, "New value:");
    access.apply_fixed_label_box(new_lbl, layout.sc_margin_x, 72, layout.sc_input_x - layout.sc_margin_x - 4, false);
    lv_obj_set_style_text_color(new_lbl, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(new_lbl, &lv_font_montserrat_12, LV_PART_MAIN);

    // Input field (digits + cursor)
    cursor_vis_ = true;
    lv_obj_t *candidate_input = lv_label_create(cont);
    if (!candidate_input) return;
    const std::string initial_display = input_buf_ + "_";
    lv_label_set_text(candidate_input, initial_display.c_str());
    access.apply_fixed_label_box(candidate_input, layout.sc_input_x, 70, layout.sc_input_width, false);
    lv_obj_set_style_text_color(candidate_input, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(candidate_input, &lv_font_montserrat_14, LV_PART_MAIN);

    // Range hint below input
    char range_buf[64];
    std::snprintf(range_buf, sizeof(range_buf), "Range: %d ~ %d",
                  detail_.min_value, detail_.max_value);
    lv_obj_t *candidate_hint = lv_label_create(cont);
    if (!candidate_hint) {
        lv_obj_delete(candidate_input);
        return;
    }
    lv_label_set_text(candidate_hint, range_buf);
    access.apply_fixed_label_box(candidate_hint, layout.sc_margin_x, 92, layout.sc_detail_text_width, false);
    lv_obj_set_style_text_color(candidate_hint, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_set_style_text_font(candidate_hint, &lv_font_montserrat_10, LV_PART_MAIN);

    lv_timer_t *candidate_timer = lv_timer_create(cursor_timer_cb, 500, this);
    if (!candidate_timer) {
        lv_obj_delete(candidate_hint);
        lv_obj_delete(candidate_input);
        return;
    }

    input_lbl_ = candidate_input;
    hint_lbl_ = candidate_hint;
    cursor_timer_ = candidate_timer;
    cursor_callback_enabled_ = true;
    lv_obj_add_event_cb(input_lbl_, SoundCard::input_label_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(hint_lbl_, SoundCard::hint_label_delete_cb, LV_EVENT_DELETE, this);

    // Bottom hint
    lv_obj_t *hint = lv_label_create(cont);
    lv_label_set_text(hint, "0-9:type  BS:del  OK:apply  ESC:back");
    access.apply_fixed_label_box(hint, layout.sc_margin_x, layout.content_height - 14, layout.sc_bottom_hint_width, true);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN);
}

void SoundCard::input_update_display()
{
    if (!input_lbl_) return;
    std::string disp = input_buf_ + (cursor_vis_ ? "_" : " ");
    lv_label_set_text(input_lbl_, disp.c_str());
}

void SoundCard::cursor_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = static_cast<SoundCard *>(lv_timer_get_user_data(timer));
    if (!self || !self->cursor_callback_enabled_ ||
        !sound_card_cursor_callback_allowed(
            timer, self->cursor_timer_, self->input_lbl_,
            self->cursor_callback_enabled_)) return;
    try {
        self->cursor_vis_ = !self->cursor_vis_;
        self->input_update_display();
    } catch (...) {
        self->cursor_callback_enabled_ = false;
    }
}

void SoundCard::cursor_stop()
{
    cursor_callback_enabled_ = false;
    if (cursor_timer_) {
        lv_timer_del(cursor_timer_);
        cursor_timer_ = nullptr;
    }
    cursor_vis_ = true;
}


} // namespace setting
