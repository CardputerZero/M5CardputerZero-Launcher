#pragma once

#include <algorithm>
#include <charconv>
#include <functional>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
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

inline bool parse_integer(const std::string &text, int &value)
{
    if (text.empty()) return false;
    const char *first = text.data();
    const char *last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

inline std::vector<Card> parse_cards(const std::string &payload)
{
    std::vector<Card> cards;
    std::istringstream lines(payload);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t separator = line.find('\t');
        if (separator == std::string::npos) continue;
        int index = -1;
        if (!parse_integer(line.substr(0, separator), index) || index < 0) continue;
        const std::string label = line.substr(separator + 1);
        if (label.empty()) continue;
        cards.push_back({index, label});
    }
    return cards;
}

inline std::vector<Control> parse_controls(const std::string &payload)
{
    std::vector<Control> controls;
    std::istringstream lines(payload);
    std::string line;
    while (std::getline(lines, line)) {
        std::vector<std::string> columns;
        std::istringstream row(line);
        std::string column;
        while (std::getline(row, column, '\t')) columns.push_back(std::move(column));
        if (columns.size() < 7 || columns[0].empty()) continue;

        Control control;
        control.name = std::move(columns[0]);
        control.type = std::move(columns[1]);
        if (!parse_integer(columns[2], control.minimum) ||
            !parse_integer(columns[3], control.maximum) ||
            !parse_integer(columns[4], control.step) ||
            !parse_integer(columns[6], control.current_value))
            continue;
        control.current_text = std::move(columns[5]);
        control.step = std::max(1, control.step);
        controls.push_back(std::move(control));
    }
    return controls;
}

inline Control parse_detail(const std::string &payload, Control fallback)
{
    std::istringstream lines(payload);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        const size_t last = line.find_last_not_of(" \t\r\n");
        const std::string text = line.substr(first, last - first + 1);

        if (text.rfind("Capabilities:", 0) == 0) {
            fallback.type = text.find("enum") != std::string::npos ? "ENUMERATED" : "INTEGER";
            continue;
        }
        if (text.rfind("Limits:", 0) == 0) {
            std::string values = text.substr(7);
            const size_t first_value = values.find_first_not_of(" \t");
            if (first_value != std::string::npos) values.erase(0, first_value);
            for (const char *prefix : {"Playback ", "Capture "}) {
                if (values.rfind(prefix, 0) == 0) {
                    values.erase(0, std::char_traits<char>::length(prefix));
                    break;
                }
            }
            const size_t split = values.find(" - ");
            if (split != std::string::npos) {
                int minimum = 0;
                int maximum = 0;
                if (parse_integer(values.substr(0, split), minimum) &&
                    parse_integer(values.substr(split + 3), maximum)) {
                    fallback.minimum = minimum;
                    fallback.maximum = maximum;
                }
            }
            continue;
        }

        static constexpr const char *value_prefixes[] = {
            "Mono:",       "Front Left:",  "Front Right:", "Rear Left:",
            "Rear Right:", "Center:",      "LFE:",         "Side Left:",
            "Side Right:", "Capture:",     "Playback:",    "Item0:",
        };
        bool value_line = false;
        for (const char *prefix : value_prefixes) {
            if (text.rfind(prefix, 0) == 0) {
                value_line = true;
                break;
            }
        }
        if (fallback.current_text.empty() && value_line && text.find(": ") != std::string::npos) {
            fallback.current_text = text;
            const size_t separator = text.find(": ");
            std::string value_text = text.substr(separator + 2);
            const size_t first_digit = value_text.find_first_of("-0123456789");
            if (first_digit != std::string::npos) {
                size_t length = first_digit + 1;
                while (length < value_text.size() &&
                       value_text[length] >= '0' && value_text[length] <= '9')
                    ++length;
                int value = fallback.current_value;
                if (parse_integer(value_text.substr(first_digit, length - first_digit), value))
                    fallback.current_value = value;
            }
        }
    }
    return fallback;
}

inline bool query(const std::list<std::string> &arguments, std::string &payload)
{
    int result_code = -1;
    bool callback_called = false;
    try {
        cp0_signal_soundcard_api(arguments, [&](int code, std::string data) {
            callback_called = true;
            result_code = code;
            payload = std::move(data);
        });
    } catch (...) {
        return false;
    }
    return callback_called && result_code == 0;
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

    void AnimateNextIn() override {}
    void AnimateNextOut() override {}
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
        lv_obj_set_size(ComponensObj, SCREEN_W, SCREEN_H);
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
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 150;

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

        int value = control_.current_value;
        if (!ui_test_soundcard::parse_integer(input_text_, value)) {
            set_status("Invalid value", 0xFF4444);
            return;
        }
        value = ui_test_soundcard::clamp_value(value, control_);

        int result_code = -1;
        try {
            cp0_signal_soundcard_api(
                {"SetControl", std::to_string(card_index_), control_.name, std::to_string(value)},
                [&](int code, std::string) { result_code = code; });
        } catch (...) {
            result_code = -1;
        }

        if (result_code == 0) {
            control_.current_value = value;
            control_.current_text = std::to_string(value);
            input_text_.clear();
            update_input_label();
            if (current_) {
                const std::string text = "Applied: " + std::to_string(value);
                lv_label_set_text(current_, text.c_str());
            }
            set_status("Applied OK", 0x33CC33);
        } else {
            set_status("Error: mixer rejected value", 0xFF4444);
        }
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
            if (!input_text_.empty())
                ui_test_soundcard::parse_integer(input_text_, base_value);
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

class LvSettingSoundCardPage4 : public DComponens::LvglComponensBase {
public:
    LvSettingSoundCardPage4() = default;

    LvSettingSoundCardPage4(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback)
        : parent_node_(parent_node)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
        load_cards();
    }

    void AnimateNextIn() override {}
    void AnimateNextOut() override {}
    void LoadNextPage() override {}
    void LeaveNextPage() override
    {
        if (LeaveSelfPage) LeaveSelfPage();
    }

    ~LvSettingSoundCardPage4() override
    {
        lv_async_call_cancel(&LvSettingSoundCardPage4::close_detail_async, this);
        detail_close_pending_ = false;
        if (detail_page_ && detail_page_->Get()) {
            if (lv_obj_get_group(detail_page_->Get()))
                lv_group_remove_obj(detail_page_->Get());
        }
        detail_page_.reset();
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
        lv_obj_set_size(ComponensObj, SCREEN_W, SCREEN_H);
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
            std::bind(&LvSettingSoundCardPage4::handle_key_event,
                      this,
                      std::placeholders::_1));
    }

private:
    enum class View { Cards, Controls };

    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 150;
    static constexpr int ROW_Y = 34;
    static constexpr int ROW_H = 20;
    static constexpr int ROW_GAP = 2;
    static constexpr int VISIBLE_ROWS = 5;

    lv_obj_t *label(const char *text,
                    int x,
                    int y,
                    int width,
                    int height,
                    uint32_t color,
                    int font_size,
                    bool scroll = false)
    {
        lv_obj_t *result = lv_label_create(ComponensObj);
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
        lv_label_set_long_mode(result, scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
        return result;
    }

    void load_cards()
    {
        view_ = View::Cards;
        selected_index_ = 0;
        card_index_ = -1;
        controls_.clear();
        std::string payload;
        backend_available_ = ui_test_soundcard::query({"ListCards"}, payload);
        cards_ = backend_available_ ? ui_test_soundcard::parse_cards(payload)
                                    : std::vector<ui_test_soundcard::Card>{};
        render();
    }

    void load_controls()
    {
        if (cards_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(cards_.size()))
            return;

        const int requested_card = cards_[selected_index_].index;
        std::string payload;
        if (!ui_test_soundcard::query(
                {"ListControls", std::to_string(requested_card)}, payload)) {
            backend_available_ = false;
            render();
            return;
        }

        controls_ = ui_test_soundcard::parse_controls(payload);
        card_index_ = requested_card;
        selected_index_ = 0;
        view_ = View::Controls;
        backend_available_ = true;
        render();
    }

    void open_detail()
    {
        if (controls_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(controls_.size()))
            return;

        ui_test_soundcard::Control control = controls_[selected_index_];
        std::string payload;
        if (ui_test_soundcard::query(
                {"GetControlDetail", std::to_string(card_index_), control.name}, payload))
            control = ui_test_soundcard::parse_detail(payload, std::move(control));

        lv_group_t *group = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr;
        detail_page_ = std::make_unique<LvSettingSoundCardDetailPage>(
            ComponensObj,
            card_index_,
            std::move(control),
            std::bind(&LvSettingSoundCardPage4::close_detail, this));
        if (!detail_page_ || !detail_page_->Get()) {
            detail_page_.reset();
            return;
        }
        if (group) {
            if (lv_obj_get_group(ComponensObj) == group)
                lv_group_remove_obj(ComponensObj);
            lv_group_add_obj(group, detail_page_->Get());
            lv_group_focus_obj(detail_page_->Get());
        }
    }

    static void close_detail_async(void *user_data)
    {
        auto *self = static_cast<LvSettingSoundCardPage4 *>(user_data);
        if (self) self->close_detail_now();
    }

    void close_detail()
    {
        if (!detail_page_ || detail_close_pending_) return;
        detail_close_pending_ = true;
        if (lv_async_call(&LvSettingSoundCardPage4::close_detail_async, this) != LV_RESULT_OK) {
            detail_close_pending_ = false;
            close_detail_now();
        }
    }

    void close_detail_now()
    {
        detail_close_pending_ = false;
        if (detail_page_ && selected_index_ >= 0 &&
            selected_index_ < static_cast<int>(controls_.size())) {
            const ui_test_soundcard::Control updated = detail_page_->control();
            if (updated.name == controls_[selected_index_].name)
                controls_[selected_index_] = updated;
        }
        lv_group_t *group = detail_page_ && detail_page_->Get()
                                ? lv_obj_get_group(detail_page_->Get())
                                : nullptr;
        if (detail_page_ && detail_page_->Get() && group)
            lv_group_remove_obj(detail_page_->Get());
        detail_page_.reset();
        if (group && ComponensObj) {
            lv_group_add_obj(group, ComponensObj);
            lv_group_focus_obj(ComponensObj);
        }
        render();
    }

    void render()
    {
        if (!ComponensObj || detail_page_) return;
        lv_obj_clean(ComponensObj);

        const bool controls_view = view_ == View::Controls;
        const char *title = controls_view ? "Mixer Controls" : "Sound Cards";
        label(title, 8, 5, 210, 20, 0x58A6FF, 14);

        if (!backend_available_) {
            label("Mixer unavailable", 8, 48, 304, 20, 0xFFAA00, 13);
            label("Soundcard backend is not supported", 8, 72, 304, 16, 0x888888, 11);
            label("ESC: back", 8, 126, 304, 14, 0x555555, 10);
            return;
        }

        const int count = controls_view ? static_cast<int>(controls_.size())
                                        : static_cast<int>(cards_.size());
        if (count == 0) {
            label(controls_view ? "No mixer controls" : "No ALSA cards found",
                  8, 54, 304, 20, 0x888888, 12);
            label("ESC: back", 8, 126, 304, 14, 0x555555, 10);
            return;
        }

        const int offset = std::clamp(selected_index_ - VISIBLE_ROWS / 2,
                                      0,
                                      std::max(0, count - VISIBLE_ROWS));
        for (int visible = 0; visible < VISIBLE_ROWS && offset + visible < count; ++visible) {
            const int index = offset + visible;
            const bool focused = index == selected_index_;
            const int y = ROW_Y + visible * (ROW_H + ROW_GAP);
            lv_obj_t *row = lv_obj_create(ComponensObj);
            if (!row) continue;
            lv_obj_set_size(row, 304, ROW_H);
            lv_obj_set_pos(row, 8, y);
            lv_obj_set_style_bg_color(row, lv_color_hex(focused ? 0x2A2A2A : 0x000000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row, focused ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            std::string name;
            std::string value;
            if (controls_view) {
                name = controls_[index].name;
                value = controls_[index].current_text;
            } else {
                name = cards_[index].label;
                value = "card " + std::to_string(cards_[index].index);
            }
            lv_obj_t *name_label = lv_label_create(row);
            if (name_label) {
                lv_label_set_text(name_label, name.c_str());
                lv_obj_set_size(name_label, controls_view ? 175 : 230, ROW_H);
                lv_obj_set_pos(name_label, 4, 0);
                lv_obj_set_style_text_color(name_label, lv_color_hex(focused ? 0xFFFFFF : 0xCCCCCC), LV_PART_MAIN);
                lv_obj_set_style_text_font(name_label, cp0_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
                lv_label_set_long_mode(name_label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            }
            if (controls_view && !value.empty()) {
                lv_obj_t *value_label = lv_label_create(row);
                if (value_label) {
                    lv_label_set_text(value_label, value.c_str());
                    lv_obj_set_size(value_label, 112, ROW_H);
                    lv_obj_set_pos(value_label, 184, 0);
                    lv_obj_set_style_text_color(value_label, lv_color_hex(focused ? 0xAADDFF : 0x777777), LV_PART_MAIN);
                    lv_obj_set_style_text_font(value_label, cp0_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
                    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
                }
            }
        }

        if (selected_index_ > 0)
            add_arrow(0, "up");
        if (selected_index_ + 1 < count)
            add_arrow(SCREEN_H - 19, "down");
        label(controls_view ? "OK: edit  ESC: back" : "OK: open  ESC: back",
              8, 126, 304, 14, 0x555555, 10);
    }

    void add_arrow(int y, const char *direction)
    {
        lv_obj_t *arrow = lv_img_create(ComponensObj);
        if (!arrow) return;
        lv_img_set_src(arrow, std::string(direction) == "up" ? &setting_red_up : &setting_red_down);
        lv_obj_update_layout(arrow);
        lv_obj_set_pos(arrow, SCREEN_W / 2 - lv_obj_get_width(arrow) / 2, y);
    }

    void handle_key(uint32_t key)
    {
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (view_ == View::Controls) {
                load_cards();
            } else if (LeaveSelfPage) {
                LeaveSelfPage();
            }
            return;
        }

        const int count = view_ == View::Controls ? static_cast<int>(controls_.size())
                                                  : static_cast<int>(cards_.size());
        if (count == 0) return;
        if (key == LV_KEY_UP) {
            selected_index_ = selected_index_ > 0 ? selected_index_ - 1 : count - 1;
            render();
        } else if (key == LV_KEY_DOWN) {
            selected_index_ = selected_index_ + 1 < count ? selected_index_ + 1 : 0;
            render();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            if (view_ == View::Cards)
                load_controls();
            else
                open_detail();
        }
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        handle_key(lv_event_get_key(event));
        lv_event_stop_processing(event);
    }

    NodeIter parent_node_;
    View view_ = View::Cards;
    std::vector<ui_test_soundcard::Card> cards_;
    std::vector<ui_test_soundcard::Control> controls_;
    int selected_index_ = 0;
    int card_index_ = -1;
    bool backend_available_ = false;
    bool detail_close_pending_ = false;
    std::unique_ptr<LvSettingSoundCardDetailPage> detail_page_;
};

inline std::unique_ptr<DComponens::LvglComponensBase> soundcard_page4_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back)
{
    return std::make_unique<LvSettingSoundCardPage4>(
        parent, page_node, std::move(on_back));
}
