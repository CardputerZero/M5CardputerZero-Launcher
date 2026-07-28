#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_ssh.hpp"
#include "../model/ssh_view_build_contract.hpp"

void UISSHPage::create_ui()
{
    if (!ui_APP_Container) return;
    auto rollback = [this]() {
        if (background_) lv_obj_delete(background_);
        background_ = nullptr;
        form_container_ = nullptr;
    };
    background_ = lv_obj_create(ui_APP_Container);
    if (!background_) return;
    lv_obj_add_event_cb(
        background_, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(background_, 320, 150);
    lv_obj_set_pos(background_, 0, 0);
    lv_obj_set_style_radius(background_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(background_, lv_color_hex(0x0D1117), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(background_, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(background_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(background_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(background_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_bar = lv_obj_create(background_);
    if (!title_bar) {
        rollback();
        return;
    }
    lv_obj_set_size(title_bar, 320, 22);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x1F3A5F), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(title_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(title_bar, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_bar);
    if (!title) {
        rollback();
        return;
    }
    lv_label_set_text(title, "SSH Client");
    lv_obj_set_align(title, LV_ALIGN_LEFT_MID);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *hint = lv_label_create(title_bar);
    if (!hint) {
        rollback();
        return;
    }
    lv_label_set_text(hint, "OK:Connect  Tab:Save  ESC:Back");
    lv_obj_set_align(hint, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(hint, -4);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x7EA8D8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (!build_input_fields()) {
        rollback();
        return;
    }
}

bool UISSHPage::build_input_fields()
{
    if (!background_) return false;
    lv_obj_t *candidate = lv_obj_create(background_);
    if (!candidate) return false;
    try {
    lv_obj_set_size(candidate, 320, 128);
    lv_obj_set_pos(candidate, 0, 22);
    lv_obj_set_style_bg_opa(candidate, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(candidate, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(candidate, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(candidate, LV_OBJ_FLAG_SCROLLABLE);

    SshViewBuildContract build(SshConnectionModel::FIELD_COUNT);
    constexpr int row_height = 30;
    constexpr int row_gap = 4;
    for (size_t index = 0; index < SshConnectionModel::FIELD_COUNT; ++index) {
        const bool active = index == model_.active_index();
        const int y = 8 + static_cast<int>(index) * (row_height + row_gap);
        lv_obj_t *row = lv_obj_create(candidate);
        if (!row) {
            lv_obj_delete(candidate);
            return false;
        }
        lv_obj_set_size(row, 300, row_height);
        lv_obj_set_pos(row, 10, y);
        lv_obj_set_style_radius(row, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(row, active ? 1 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(row, lv_color_hex(0x1F6FEB), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(row, lv_color_hex(active ? 0x1F3A5F : 0x161B22),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        if (active) {
            lv_obj_t *bar = lv_obj_create(row);
            if (!bar) {
                lv_obj_delete(candidate);
                return false;
            }
            lv_obj_set_size(bar, 3, row_height - 8);
            lv_obj_set_pos(bar, 2, 3);
            lv_obj_set_style_radius(bar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x1F6FEB), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        }

        lv_obj_t *label = lv_label_create(row);
        if (!label) {
            lv_obj_delete(candidate);
            return false;
        }
        lv_label_set_text(label, model_.label(index));
        lv_obj_set_pos(label, 10, 8);
        lv_obj_set_style_text_color(label, lv_color_hex(active ? 0x58A6FF : 0x7EA8D8),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);

        std::string display = model_.value(index);
        if (active) display += "_";
        lv_obj_t *value = lv_label_create(row);
        if (!value) {
            lv_obj_delete(candidate);
            return false;
        }
        lv_label_set_text(value, display.c_str());
        lv_obj_set_pos(value, 60, 7);
        lv_obj_set_width(value, 228);
        lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(value, lv_color_hex(active ? 0xFFFFFF : 0xCCCCCC),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(value, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        build.row_completed();
    }

    lv_obj_t *hint = lv_label_create(candidate);
    if (!hint) {
        lv_obj_delete(candidate);
        return false;
    }
    lv_label_set_text(hint, status_message_.empty()
        ? "UP/DN:field  Type  OK:Connect  Tab:Save"
        : status_message_.c_str());
    lv_obj_set_pos(hint, 10, 113);
    lv_obj_set_width(hint, 300);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(hint, lv_color_hex(status_message_.empty() ? 0x777777 :
                                                   (status_error_ ? 0xFF6B6B : 0x69AD80)),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
    build.hint_completed();
    if (!build.ready()) {
        lv_obj_delete(candidate);
        return false;
    }

    lv_obj_add_event_cb(
        candidate, static_owned_obj_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_t *old = form_container_;
    form_container_ = candidate;
    if (old) lv_obj_delete(old);
    return true;
    } catch (...) {
        lv_obj_delete(candidate);
        return false;
    }
}
