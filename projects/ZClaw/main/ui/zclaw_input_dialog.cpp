#include "zclaw_input_dialog.h"

#include "zclaw_fonts.hpp"
#include "zclaw_theme.h"
#include "keyboard_input.h"

namespace zclaw {
namespace {

constexpr lv_coord_t kDialogHeight = 96;
// Keep the original 113 px dialog top at y=57 while shortening the bottom.
constexpr lv_coord_t kDialogTop = 57;

}  // namespace

InputDialog::~InputDialog()
{
    close();
    if (cursor_style_initialized_)
        lv_style_reset(&cursor_style_);
}

void InputDialog::open_chat(const FontManager *fonts)
{
    mode_ = InputMode::Chat;
    open(fonts);
    if (textarea_)
        lv_textarea_set_placeholder_text(textarea_, "Type your message...");
}

void InputDialog::open_text(const FontManager *fonts, const std::string &placeholder,
                            const std::string &initial_text, InputMode mode,
                            bool secret)
{
    mode_ = mode;
    secret_ = secret;
    secret_revealed_ = false;
    open(fonts);
    if (!textarea_)
        return;
    lv_textarea_set_placeholder_text(textarea_, placeholder.c_str());
    lv_textarea_set_password_mode(textarea_, secret_);
    lv_textarea_set_text(textarea_, initial_text.c_str());
    lv_textarea_set_cursor_pos(textarea_, LV_TEXTAREA_CURSOR_LAST);
    keep_single_line_cursor_visible();
}

void InputDialog::close()
{
    lv_obj_t *dialog = dialog_;
    if (dialog)
        lv_obj_del(dialog);
    release_dialog();
}

bool InputDialog::is_open() const
{
    return dialog_ && textarea_;
}

InputMode InputDialog::mode() const
{
    return mode_;
}

std::string InputDialog::text() const
{
    if (!textarea_)
        return "";
    const char *value = lv_textarea_get_text(textarea_);
    return value ? value : "";
}

void InputDialog::append(const char *utf8)
{
    if (textarea_ && utf8 && utf8[0]) {
        lv_textarea_add_text(textarea_, utf8);
        keep_single_line_cursor_visible();
    }
}

void InputDialog::insert_newline()
{
    if (textarea_)
        lv_textarea_add_char(textarea_, '\n');
}

void InputDialog::erase_before_cursor()
{
    if (textarea_) {
        lv_textarea_delete_char(textarea_);
        keep_single_line_cursor_visible();
    }
}

void InputDialog::erase_after_cursor()
{
    if (textarea_) {
        lv_textarea_delete_char_forward(textarea_);
        keep_single_line_cursor_visible();
    }
}

void InputDialog::move_left()
{
    if (textarea_) {
        lv_textarea_cursor_left(textarea_);
        keep_single_line_cursor_visible();
    }
}

void InputDialog::move_right()
{
    if (textarea_) {
        lv_textarea_cursor_right(textarea_);
        keep_single_line_cursor_visible();
    }
}

void InputDialog::move_up()
{
    if (textarea_)
        lv_textarea_cursor_up(textarea_);
}

void InputDialog::move_down()
{
    if (textarea_)
        lv_textarea_cursor_down(textarea_);
}

void InputDialog::toggle_secret_visibility()
{
    if (!textarea_ || !secret_)
        return;
    secret_revealed_ = !secret_revealed_;
    lv_textarea_set_password_mode(textarea_, !secret_revealed_);
}

void InputDialog::open(const FontManager *fonts)
{
    if (is_open() || !fonts)
        return;

    const bool single_line = input_is_single_line(mode_);
    dialog_ = lv_msgbox_create(lv_layer_top());
    lv_obj_add_event_cb(dialog_, dialog_deleted, LV_EVENT_DELETE, this);
    lv_obj_set_size(dialog_, 300, kDialogHeight);
    lv_obj_align(dialog_, LV_ALIGN_TOP_MID, 0, kDialogTop);
    lv_obj_set_style_radius(dialog_, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dialog_, lv_color_hex(theme::kBar), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dialog_, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dialog_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(dialog_, lv_color_hex(theme::kPanelLine),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(dialog_, lv_color_hex(theme::kText), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(dialog_, fonts->font_10(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(dialog_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *content = lv_msgbox_get_content(dialog_);
    lv_obj_set_style_pad_all(content, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    textarea_ = lv_textarea_create(content);
    lv_textarea_set_one_line(textarea_, single_line);
    lv_obj_set_size(textarea_, 290, kDialogHeight - 10);
    lv_textarea_set_placeholder_text(textarea_, "Type your message...");
    lv_textarea_set_cursor_click_pos(textarea_, false);
    lv_obj_set_style_radius(textarea_, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(textarea_, lv_color_hex(theme::kPanel), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(textarea_, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(textarea_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(textarea_, lv_color_hex(theme::kWhite), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(textarea_, fonts->font_10(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(textarea_, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (!cursor_style_initialized_) {
        lv_style_init(&cursor_style_);
        lv_style_set_bg_opa(&cursor_style_, LV_OPA_TRANSP);
        lv_style_set_border_color(&cursor_style_, lv_color_hex(theme::kPurple));
        lv_style_set_border_side(&cursor_style_, LV_BORDER_SIDE_LEFT);
        lv_style_set_border_width(&cursor_style_, 2);
        lv_style_set_pad_left(&cursor_style_, -4);
        lv_style_set_pad_right(&cursor_style_, 0);
        cursor_style_initialized_ = true;
    }
    lv_obj_add_style(textarea_, &cursor_style_, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_state(textarea_, LV_STATE_FOCUSED);
    lv_obj_send_event(textarea_, LV_EVENT_FOCUSED, nullptr);
    lv_textarea_set_cursor_pos(textarea_, LV_TEXTAREA_CURSOR_LAST);
    cp0_keyboard_set_lvgl_keypad_intercept(1);
}

void InputDialog::keep_single_line_cursor_visible()
{
    if (!textarea_ || !lv_textarea_get_one_line(textarea_))
        return;

    lv_obj_update_layout(textarea_);
    lv_obj_t *label = lv_textarea_get_label(textarea_);
    lv_point_t cursor{};
    lv_label_get_letter_pos(label, lv_textarea_get_cursor_pos(textarea_), &cursor);

    constexpr lv_coord_t kCaretMargin = 4;
    const lv_coord_t viewport_width = lv_obj_get_content_width(textarea_);
    const lv_coord_t scroll_left = lv_obj_get_scroll_left(textarea_);
    if (cursor.x < scroll_left + kCaretMargin) {
        lv_obj_scroll_to_x(textarea_, cursor.x - kCaretMargin, LV_ANIM_OFF);
    }
    else if (cursor.x + kCaretMargin > scroll_left + viewport_width) {
        lv_obj_scroll_to_x(textarea_,
                           cursor.x + kCaretMargin - viewport_width,
                           LV_ANIM_OFF);
    }
}

void InputDialog::dialog_deleted(lv_event_t *event)
{
    InputDialog *dialog =
        static_cast<InputDialog *>(lv_event_get_user_data(event));
    if (dialog)
        dialog->release_dialog();
}

void InputDialog::release_dialog()
{
    cp0_keyboard_set_lvgl_keypad_intercept(0);
    dialog_ = nullptr;
    textarea_ = nullptr;
    secret_ = false;
    secret_revealed_ = false;
    mode_ = InputMode::Chat;
}

}  // namespace zclaw
