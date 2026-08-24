#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cp0_font_service.hpp"
#include "hal_lvgl_bsp.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "setting_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_componens.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

extern "C" {
extern const lv_image_dsc_t setting_red_up;
extern const lv_image_dsc_t setting_red_down;
}

namespace ui_test_soundcard {

struct Card {
    int index = -1;
    std::string label;
};

struct Control {
    std::string name;
    std::string type;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    std::string current_text;
    int current_value = 0;
};

inline std::vector<Card> mock_cards()
{
    return {
        {0, "Mock Built-in Audio"},
        {1, "Mock USB Audio"},
    };
}

inline std::vector<Control> mock_controls(int card_index)
{
    const int volume_offset = card_index == 1 ? -10 : 0;
    return {
        {"Master Volume", "INTEGER", 0, 100, 5,
         std::to_string(65 + volume_offset), 65 + volume_offset},
        {"PCM Volume", "INTEGER", 0, 100, 5,
         std::to_string(80 + volume_offset), 80 + volume_offset},
        {"Mic Boost", "INTEGER", 0, 20, 1, "3", 3},
        {"Headphone Volume", "INTEGER", 0, 100, 5,
         std::to_string(70 + volume_offset), 70 + volume_offset},
    };
}

inline int clamp_value(int value, const Control &control)
{
    if (control.maximum < control.minimum) return value;
    return std::clamp(value, control.minimum, control.maximum);
}

} // namespace ui_test_soundcard

class LvSettingSoundCardDetailPage : public DComponens::LvglComponensBase {
public:
    using Control = ui_test_soundcard::Control;

    LvSettingSoundCardDetailPage() = default;

    LvSettingSoundCardDetailPage(lv_obj_t *parent,
                                 int card_index,
                                 Control control,
                                 std::function<void()> back_callback)
        : card_index_(card_index), control_(std::move(control))
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
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_TRANSP, LV_PART_MAIN);
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

        title_ = create_label(control_.name.c_str(), 8, 6, 180, 16, 0x58A6FF, 14);
        card_ = create_label(card_label().c_str(), 8, 26, 304, 14, 0xAAAAAA, 11);
        limits_ = create_label(limits_text().c_str(), 8, 44, 304, 14, 0xAAAAAA, 11);
        current_ = create_label(control_.current_text.c_str(), 8, 62, 304, 16, 0xCCCCCC, 11);
        input_ = create_label("value: _", 8, 84, 304, 18, 0xFFFFFF, 14);
        status_ = create_label("OK: apply  ESC: back", 8, 112, 304, 14, 0x555555, 10);
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

    lv_obj_t *create_label(const char *text,
                           int x,
                           int y,
                           int width,
                           int height,
                           uint32_t color,
                           int font_size)
    {
        lv_obj_t *label = lv_label_create(ComponensObj);
        if (!label) return nullptr;
        lv_label_set_text(label, text ? text : "");
        lv_obj_set_size(label, width, height);
        lv_obj_set_pos(label, x, y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_text_font(
            label,
            cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD),
            LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        return label;
    }

    std::string card_label() const
    {
        return "Card " + std::to_string(card_index_);
    }

    std::string limits_text() const
    {
        return "Range: " + std::to_string(control_.minimum) + " - " +
               std::to_string(control_.maximum) + "  " + control_.type;
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
        if (digit < 0 || digit > 9 || input_text_.size() >= 8) return;
        input_text_.push_back(static_cast<char>('0' + digit));
        update_input_label();
    }

    void update_input_label()
    {
        if (!input_) return;
        const std::string text = "value: " + (input_text_.empty() ? "_" : input_text_);
        lv_label_set_text(input_, text.c_str());
    }

    void set_status(const char *text, uint32_t color)
    {
        if (!status_) return;
        lv_label_set_text(status_, text ? text : "");
        lv_obj_set_style_text_color(status_, lv_color_hex(color), LV_PART_MAIN);
    }

    void apply_value()
    {
        if (input_text_.empty()) {
            set_status("Type a value first", 0xFFAA00);
            return;
        }

        int value = 0;
        try {
            size_t parsed_length = 0;
            value = std::stoi(input_text_, &parsed_length);
            if (parsed_length != input_text_.size()) {
                set_status("Invalid value", 0xFF4444);
                return;
            }
        } catch (...) {
            set_status("Invalid value", 0xFF4444);
            return;
        }
        value = ui_test_soundcard::clamp_value(value, control_);

        control_.current_value = value;
        control_.current_text = std::to_string(value);
        input_text_.clear();
        update_input_label();
        if (current_) {
            const std::string text = "Applied: " + std::to_string(value);
            lv_label_set_text(current_, text.c_str());
        }
        set_status("Applied (mock)", 0x33CC33);
    }

    void handle_key(uint32_t key)
    {
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (LeaveSelfPage) LeaveSelfPage();
            return;
        }
        if (key == LV_KEY_BACKSPACE) {
            if (!input_text_.empty()) input_text_.pop_back();
            update_input_label();
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
            const int candidate = ui_test_soundcard::clamp_value(
                base_value + direction * control_.step, control_);
            input_text_ = std::to_string(candidate);
            update_input_label();
            return;
        }
        if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            apply_value();
        }
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
        if (!self || !item) return;

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
    std::string input_text_;
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
    int card_index, ui_test_soundcard::Control control)
{
    return [card_index, control = std::move(control)](
               lv_obj_t *parent, const NodeIter &, std::function<void()> on_back) mutable {
        return std::make_unique<LvSettingSoundCardDetailPage>(
            parent, card_index, std::move(control), std::move(on_back));
    };
}
