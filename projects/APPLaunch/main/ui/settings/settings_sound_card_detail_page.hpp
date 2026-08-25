#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "cp0_font_service.hpp"
#include "cp0_lvgl_app_page_assets.h"
#include "hal_lvgl_bsp.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "settings_sound_card_adapter.hpp"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

class LvSettingSoundCardDetailPage : public DComponens::LvglComponensBase {
public:
    using Control = ui_test_soundcard::Control;
    using SubmitCallback = std::function<void(std::string)>;

    LvSettingSoundCardDetailPage() = default;

    LvSettingSoundCardDetailPage(lv_obj_t *parent,
                                 int card_index,
                                 Control control,
                                 std::function<void()> back_callback,
                                 SubmitCallback submit_callback = {})
        : card_index_(card_index),
          control_(std::move(control)),
          submit_callback_(std::move(submit_callback))
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
    }

    void AnimateNextIn(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }

    void AnimateNextOut(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }

    void LoadNextPage() override {}

    void LeaveNextPage() override
    {
        if (LeaveSelfPage) LeaveSelfPage();
    }

    const Control &control() const
    {
        return control_;
    }

    bool detail_loaded() const
    {
        return detail_loaded_;
    }

    bool writing() const
    {
        return writing_;
    }

    void set_detail(Control detail)
    {
        if (detail.type.empty()) detail.type = control_.type;
        if (detail.current_text.empty()) {
            detail.current_text = control_.current_text;
            if (detail.type != "ENUMERATED") detail.current_value = control_.current_value;
        }
        if (detail.maximum == 0 && control_.maximum != 0) {
            detail.minimum = control_.minimum;
            detail.maximum = control_.maximum;
        }
        control_ = std::move(detail);
        detail_loaded_ = true;
        writing_ = false;
        loading_ = false;
        input_text_.clear();
        select_current_option();
        set_status("OK: apply  ESC: back", 0x555555);
        update_labels();
    }

    void set_detail_error(const std::string &message)
    {
        detail_loaded_ = false;
        loading_ = false;
        writing_ = false;
        input_text_.clear();
        set_status(message.empty() ? "Unable to read control" : message, 0xFF4444);
        update_labels();
    }

    void begin_write(const std::string &wire_value)
    {
        writing_ = true;
        pending_wire_value_ = wire_value;
        input_text_.clear();
        set_status("Applying...", 0xFFAA00);
        update_labels();
    }

    void mark_refresh_pending()
    {
        writing_ = true;
        set_status("Reading actual value...", 0xFFAA00);
        update_labels();
    }

    void complete_write_success(Control actual)
    {
        if (actual.type.empty()) actual.type = control_.type;
        if (actual.name.empty()) actual.name = control_.name;
        control_ = std::move(actual);
        detail_loaded_ = true;
        loading_ = false;
        writing_ = false;
        pending_wire_value_.clear();
        input_text_.clear();
        select_current_option();
        set_status("Applied OK", 0x33CC33);
        update_labels();
    }

    void complete_write_failure(const std::string &message)
    {
        writing_ = false;
        input_text_ = pending_wire_value_;
        set_status(message.empty() ? "Write failed" : message, 0xFF4444);
        update_labels();
    }

    ~LvSettingSoundCardDetailPage() override
    {
        if (event_root_ && keyboard_event_dsc_) {
            lv_obj_remove_event_dsc(event_root_, keyboard_event_dsc_);
            keyboard_event_dsc_ = nullptr;
        }
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;

        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj,
                        metric(LayoutMetric::ScreenW),
                        metric(LayoutMetric::ScreenH));
        lv_obj_set_pos(ComponensObj, 0, 0);
        lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
        DComponens::lvgl_bind_event(
            ComponensObj,
            LV_EVENT_KEY,
            nullptr,
            std::bind(&LvSettingSoundCardDetailPage::handle_key_event,
                      this,
                      std::placeholders::_1));

        event_root_ = lv_screen_active();
        if (event_root_ && LV_EVENT_KEYBOARD != 0) {
            keyboard_event_dsc_ = lv_obj_add_event_cb(
                event_root_,
                &LvSettingSoundCardDetailPage::keyboard_event_cb,
                static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD),
                this);
        }

        title_ = create_label(ComponensObj, control_.name.c_str(), 8, 6, 180, 16, 0x58A6FF, 14);
        card_ = create_label(ComponensObj, card_label().c_str(), 8, 26, 304, 14, 0xAAAAAA, 11);
        limits_ = create_label(ComponensObj, limits_text().c_str(), 8, 44, 304, 14, 0xAAAAAA, 11);
        current_ = create_label(ComponensObj, current_text().c_str(), 8, 62, 304, 16, 0xCCCCCC, 11);
        input_ = create_label(ComponensObj, "value: _", 8, 84, 304, 18, 0xFFFFFF, 14);
        status_ = create_label(ComponensObj, "Loading detail...", 8, 112, 304, 14, 0xFFAA00, 10);
        update_labels();
    }

private:
    enum class LayoutMetric : int {
        ScreenW = 320,
        ScreenH = 150,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    static lv_obj_t *create_label(lv_obj_t *parent,
                                  const char *text,
                                  int x,
                                  int y,
                                  int width,
                                  int height,
                                  uint32_t color,
                                  int font_size)
    {
        lv_obj_t *result = lv_label_create(parent);
        if (!result) return nullptr;
        lv_label_set_text(result, text ? text : "");
        lv_obj_set_size(result, width, height);
        lv_obj_set_pos(result, x, y);
        lv_obj_set_style_text_color(result, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_text_font(
            result,
            cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD),
            LV_PART_MAIN);
        lv_obj_set_style_text_align(result, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_label_set_long_mode(result, LV_LABEL_LONG_CLIP);
        return result;
    }

    std::string card_label() const
    {
        return "Card " + std::to_string(card_index_);
    }

    std::string limits_text() const
    {
        if (control_.type == "ENUMERATED")
            return "Type: ENUMERATED  options: " + std::to_string(control_.options.size());
        if (control_.type.empty()) return "Type: unavailable";
        return "Range: " + std::to_string(control_.minimum) + " - " +
               std::to_string(control_.maximum) + "  step " + std::to_string(control_.step) +
               "  " + control_.type;
    }

    std::string current_text() const
    {
        if (control_.current_text.empty()) return "Current: unavailable";
        return "Current: " + control_.current_text;
    }

    void update_labels()
    {
        if (title_) lv_label_set_text(title_, control_.name.c_str());
        if (card_) {
            const std::string text = card_label();
            lv_label_set_text(card_, text.c_str());
        }
        if (limits_) {
            const std::string text = limits_text();
            lv_label_set_text(limits_, text.c_str());
        }
        if (current_) {
            const std::string text = current_text();
            lv_label_set_text(current_, text.c_str());
        }
        if (input_) {
            const std::string text = input_text_.empty() ? "value: _" : "value: " + input_text_;
            lv_label_set_text(input_, text.c_str());
        }
        if (status_ && !detail_loaded_ && loading_)
            set_status("Loading detail...", 0xFFAA00);
    }

    void set_status(const std::string &text, uint32_t color)
    {
        if (!status_) return;
        lv_label_set_text(status_, text.c_str());
        lv_obj_set_style_text_color(status_, lv_color_hex(color), LV_PART_MAIN);
    }

    void select_current_option()
    {
        selected_option_ = 0;
        if (control_.options.empty()) return;
        if (!control_.current_option.empty()) {
            const auto found = std::find(control_.options.begin(),
                                         control_.options.end(),
                                         control_.current_option);
            if (found != control_.options.end()) {
                selected_option_ = static_cast<int>(
                    std::distance(control_.options.begin(), found));
                return;
            }
        }
        if (!input_text_.empty()) {
            const auto found = std::find(control_.options.begin(),
                                         control_.options.end(),
                                         input_text_);
            if (found != control_.options.end())
                selected_option_ = static_cast<int>(
                    std::distance(control_.options.begin(), found));
        }
    }

    static int digit_from_key(uint32_t key)
    {
        if (key >= KEY_1 && key <= KEY_9)
            return static_cast<int>(key - KEY_1 + 1);
        if (key == KEY_0) return 0;
        if (key >= static_cast<uint32_t>('0') && key <= static_cast<uint32_t>('9'))
            return static_cast<int>(key - static_cast<uint32_t>('0'));
        return -1;
    }

    void append_digit(int digit)
    {
        if (!detail_loaded_ || writing_ || control_.type == "ENUMERATED" ||
            digit < 0 || digit > 9 || input_text_.size() >= 8)
            return;
        input_text_.push_back(static_cast<char>('0' + digit));
        update_labels();
    }

    void select_enum_option(int direction)
    {
        if (!detail_loaded_ || writing_) return;
        if (control_.options.empty()) {
            set_status("No enum options", 0xFFAA00);
            return;
        }
        const int count = static_cast<int>(control_.options.size());
        selected_option_ = (selected_option_ + direction) % count;
        if (selected_option_ < 0) selected_option_ += count;
        input_text_ = control_.options[static_cast<size_t>(selected_option_)];
        update_labels();
    }

    void submit_value()
    {
        if (!detail_loaded_ || writing_) return;
        if (!control_.writable) {
            set_status("Control is read-only", 0xFFAA00);
            return;
        }

        std::string wire_value;
        if (control_.type == "ENUMERATED") {
            if (control_.options.empty()) {
                set_status("Control cannot be edited", 0xFFAA00);
                return;
            }
            if (input_text_.empty())
                input_text_ = control_.options[static_cast<size_t>(selected_option_)];
            const auto found = std::find(control_.options.begin(),
                                         control_.options.end(),
                                         input_text_);
            if (found == control_.options.end()) {
                set_status("Invalid option", 0xFF4444);
                return;
            }
            wire_value = input_text_;
        } else {
            if (input_text_.empty()) {
                set_status("Type a value first", 0xFFAA00);
                return;
            }
            size_t parsed_length = 0;
            int value = 0;
            try {
                value = std::stoi(input_text_, &parsed_length);
            } catch (...) {
                set_status("Invalid value", 0xFF4444);
                return;
            }
            if (parsed_length != input_text_.size()) {
                set_status("Invalid value", 0xFF4444);
                return;
            }
            value = ui_test_soundcard::SoundCardModel::clamp_value(value, control_);
            wire_value = std::to_string(value);
            input_text_ = wire_value;
        }

        if (!ui_test_soundcard::SoundCardModel::has_control_name(control_) ||
            !ui_test_soundcard::SoundCardModel::has_safe_wire_value(wire_value)) {
            set_status("Invalid control value", 0xFF4444);
            return;
        }
        if (submit_callback_) {
            submit_callback_(std::move(wire_value));
        } else {
            begin_write(wire_value);
            complete_write_failure("Soundcard backend unavailable");
        }
    }

    void handle_key(uint32_t key)
    {
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (LeaveSelfPage) LeaveSelfPage();
            return;
        }
        if (!detail_loaded_ || loading_ || writing_) return;
        if (control_.type == "ENUMERATED" && (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
            select_enum_option(key == LV_KEY_UP ? 1 : -1);
            return;
        }
        if (key == LV_KEY_BACKSPACE) {
            if (!input_text_.empty()) input_text_.pop_back();
            update_labels();
            return;
        }
        if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
            const int direction = key == LV_KEY_UP ? 1 : -1;
            int base_value = control_.current_value;
            if (!input_text_.empty()) {
                try {
                    base_value = std::stoi(input_text_);
                } catch (...) {
                    base_value = control_.current_value;
                }
            }
            const int candidate = ui_test_soundcard::SoundCardModel::clamp_value(
                base_value + direction * control_.step, control_);
            input_text_ = std::to_string(candidate);
            update_labels();
            return;
        }
        if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) submit_value();
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        handle_key(lv_event_get_key(event));
        lv_event_stop_processing(event);
    }

    static void keyboard_event_cb(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD))
            return;
        auto *self = static_cast<LvSettingSoundCardDetailPage *>(
            lv_event_get_user_data(event));
        auto *item = static_cast<const key_item *>(lv_event_get_param(event));
        if (!self || !item || self->control_.type == "ENUMERATED") return;

        if (item->utf8[0] >= '0' && item->utf8[0] <= '9') {
            self->append_digit(item->utf8[0] - '0');
            lv_event_stop_processing(event);
            return;
        }

        if (item->key_state != KBD_KEY_PRESSED && item->key_state != KBD_KEY_REPEATED)
            return;
        const int digit = digit_from_key(item->key_code);
        if (digit >= 0) {
            self->append_digit(digit);
            lv_event_stop_processing(event);
        }
    }

    int card_index_ = -1;
    Control control_;
    SubmitCallback submit_callback_;
    std::string input_text_;
    std::string pending_wire_value_;
    int selected_option_ = 0;
    bool loading_ = true;
    bool detail_loaded_ = false;
    bool writing_ = false;
    lv_obj_t *event_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *card_ = nullptr;
    lv_obj_t *limits_ = nullptr;
    lv_obj_t *current_ = nullptr;
    lv_obj_t *input_ = nullptr;
    lv_obj_t *status_ = nullptr;
};

inline SettingPageFactory soundcard_detail_page_factory(
    int card_index,
    ui_test_soundcard::Control control,
    LvSettingSoundCardDetailPage::SubmitCallback submit_callback = {})
{
    return [card_index,
            control = std::move(control),
            submit_callback = std::move(submit_callback)](
               lv_obj_t *parent, const NodeIter &, std::function<void()> on_back) mutable {
        return std::make_unique<LvSettingSoundCardDetailPage>(
            parent,
            card_index,
            std::move(control),
            std::move(on_back),
            std::move(submit_callback));
    };
}
