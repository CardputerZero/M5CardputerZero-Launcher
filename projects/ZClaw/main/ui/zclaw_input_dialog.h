#pragma once

#include "lvgl/lvgl.h"
#include "zclaw_input_model.h"

#include <string>

namespace zclaw {

class FontManager;

class InputDialog {
public:
    InputDialog() = default;
    ~InputDialog();

    InputDialog(const InputDialog &) = delete;
    InputDialog &operator=(const InputDialog &) = delete;

    void open_chat(const FontManager *fonts);
    void open_text(const FontManager *fonts, const std::string &placeholder,
                   const std::string &initial_text, InputMode mode,
                   bool secret = false);
    void close();

    bool is_open() const;
    InputMode mode() const;
    std::string text() const;

    void append(const char *utf8);
    void insert_newline();
    void erase_before_cursor();
    void erase_after_cursor();
    void move_left();
    void move_right();
    void move_up();
    void move_down();
    void toggle_secret_visibility();

private:
    static void dialog_deleted(lv_event_t *event);
    void open(const FontManager *fonts);
    void keep_single_line_cursor_visible();
    void release_dialog();

    lv_obj_t *dialog_ = nullptr;
    lv_obj_t *textarea_ = nullptr;
    lv_style_t cursor_style_{};
    bool cursor_style_initialized_ = false;
    bool secret_ = false;
    bool secret_revealed_ = false;
    InputMode mode_ = InputMode::Chat;
};

}  // namespace zclaw
