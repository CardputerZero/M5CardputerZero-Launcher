#pragma once

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "cp0_bounded_task_registry.hpp"
#include "cp0_font_service.hpp"
#include "cp0_lvgl_app.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "setting_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_componens.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

struct Cp0WifiHiddenUiDispatch {
    std::mutex mutex;
    bool stopped = false;
    std::deque<std::function<void()>> pending;
};

class LvSettingWifiHiddenPage3 : public DComponens::LvglComponensBase {
public:
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 150;
    static constexpr int MAX_SSID_BYTES = 32;
    static constexpr int MAX_PASSWORD_BYTES = 64;
    static constexpr int FIELD_TEXT_X = 6;
    static constexpr int FIELD_TEXT_RIGHT_INSET = 6;
    static constexpr int CURSOR_GAP = 2;
    static constexpr int CURSOR_WIDTH = 2;
    static constexpr int CURSOR_HEIGHT = 16;

    LvSettingWifiHiddenPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback,
                             bool wifi_power_enabled)
        : parent_node_(parent_node), wifi_power_enabled_(wifi_power_enabled)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
    }

    void AnimateNextIn(std::function<void()> callback) override
    {
        if (callback) callback();
    }
    void AnimateNextOut(std::function<void()> callback) override
    {
        if (callback) callback();
    }
    void LoadNextPage() override {}
    void LeaveNextPage() override
    {
        if (LeaveSelfPage) LeaveSelfPage();
    }

    ~LvSettingWifiHiddenPage3() override
    {
        if (keyboard_root_ && keyboard_event_dsc_) {
            lv_obj_remove_event_dsc(keyboard_root_, keyboard_event_dsc_);
            keyboard_event_dsc_ = nullptr;
        }
        if (cursor_timer_) {
            lv_timer_delete(cursor_timer_);
            cursor_timer_ = nullptr;
        }
        restore_text_input_mode();
        {
            std::lock_guard<std::mutex> lock(dispatch_->mutex);
            dispatch_->stopped = true;
            dispatch_->pending.clear();
        }
        lifetime_.reset();
        api_tasks_.join_all();
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
            std::bind(&LvSettingWifiHiddenPage3::handle_key_event,
                      this,
                      std::placeholders::_1));

        keyboard_root_ = lv_screen_active();
        if (keyboard_root_ && LV_EVENT_KEYBOARD != 0) {
            keyboard_event_dsc_ = lv_obj_add_event_cb(
                keyboard_root_,
                keyboard_event_cb,
                static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD),
                this);
        }
        previous_input_context_ = cp0_keyboard_get_input_context();
        previous_keypad_intercept_ = cp0_keyboard_get_lvgl_keypad_intercept();
        cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_TEXT);
        cp0_keyboard_set_lvgl_keypad_intercept(1);
        cursor_timer_ = lv_timer_create(cursor_timer_cb, 500, this);
        render();
        if (!wifi_power_enabled_) show_power_warning();
    }

private:
    static lv_obj_t *create_label(lv_obj_t *parent,
                                  const char *text,
                                  int x,
                                  int y,
                                  int width,
                                  uint32_t color,
                                  const lv_font_t *font)
    {
        if (!parent) return nullptr;
        lv_obj_t *label = lv_label_create(parent);
        if (!label) return nullptr;
        lv_label_set_text(label, text ? text : "");
        lv_obj_set_pos(label, x, y);
        if (width > 0) {
            lv_obj_set_width(label, width);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        }
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        return label;
    }

    static std::size_t previous_utf8_start(const std::string &value, std::size_t cursor)
    {
        if (cursor == 0 || cursor > value.size()) return 0;
        std::size_t start = cursor - 1;
        while (start > 0 &&
               (static_cast<unsigned char>(value[start]) & 0xC0u) == 0x80u)
            --start;
        return start;
    }

    static std::size_t next_utf8_end(const std::string &value, std::size_t cursor)
    {
        if (cursor >= value.size()) return value.size();
        std::size_t end = cursor + 1;
        while (end < value.size() &&
               (static_cast<unsigned char>(value[end]) & 0xC0u) == 0x80u)
            ++end;
        return end;
    }

    static std::string masked(const std::string &value)
    {
        return std::string(value.size(), '*');
    }

    std::string &current_value()
    {
        return field_ == 0 ? ssid_ : password_;
    }

    const std::string &current_value() const
    {
        return field_ == 0 ? ssid_ : password_;
    }

    std::size_t &current_cursor()
    {
        return field_ == 0 ? ssid_cursor_ : password_cursor_;
    }

    void restore_text_input_mode()
    {
        if (!input_mode_saved_) return;
        cp0_keyboard_set_input_context(previous_input_context_);
        cp0_keyboard_set_lvgl_keypad_intercept(previous_keypad_intercept_);
        input_mode_saved_ = false;
    }

    void render()
    {
        if (!ComponensObj || warning_active_) return;
        lv_obj_clean(ComponensObj);
        create_label(ComponensObj,
                     "Add Hidden WiFi",
                     8,
                     5,
                     SCREEN_W - 16,
                     0x58A6FF,
                     cp0_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD));

        render_field("SSID", 0, ssid_, ssid_cursor_, 31, MAX_SSID_BYTES);
        const std::string password_display = password_visible_ ? password_ : masked(password_);
        render_field("PASSWORD", 1, password_display, password_cursor_, 66, MAX_PASSWORD_BYTES);

        const char *hint = saving_ ? "Connecting..." :
            password_visible_ ? "TAB:switch ALT:hide OK:connect ESC:back" :
                                 "TAB:switch ALT:show OK:connect ESC:back";
        create_label(ComponensObj, hint, 8, SCREEN_H - 14, SCREEN_W - 16,
                     saving_ ? 0xFFAA00 : 0x555555, &lv_font_montserrat_10);
        if (!error_.empty())
            create_label(ComponensObj, error_.c_str(), 8, 103, SCREEN_W - 16,
                         0xFF4444, &lv_font_montserrat_10);
    }

    void render_field(const char *name,
                      int field_index,
                      const std::string &value,
                      std::size_t cursor,
                      int y,
                      std::size_t max_bytes)
    {
        lv_obj_t *box = lv_obj_create(ComponensObj);
        if (!box) return;
        lv_obj_set_size(box, SCREEN_W - 88, 25);
        lv_obj_set_pos(box, 80, y);
        lv_obj_set_style_radius(box, 2, LV_PART_MAIN);
        lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(box,
                                      lv_color_hex(field_ == field_index ? 0x58A6FF : 0x444444),
                                      LV_PART_MAIN);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x111111), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        create_label(ComponensObj, name, 8, y + 5, 68, 0xCCCCCC, &lv_font_montserrat_10);

        const bool focused = field_ == field_index;
        if (!focused || !cursor_visible_) {
            create_label(box,
                         value.c_str(),
                         FIELD_TEXT_X,
                         4,
                         SCREEN_W - 88 - FIELD_TEXT_X - FIELD_TEXT_RIGHT_INSET,
                         0xFFFFFF, &lv_font_montserrat_12);
            (void)max_bytes;
            return;
        }

        const std::size_t split = std::min(cursor, value.size());
        const std::string prefix = value.substr(0, split);
        const std::string suffix = value.substr(split);
        const int box_width = SCREEN_W - 88;
        const int field_right = box_width - FIELD_TEXT_RIGHT_INSET;
        const int max_prefix_width = std::max(
            0,
            field_right - FIELD_TEXT_X - CURSOR_GAP - CURSOR_WIDTH - CURSOR_GAP);
        lv_obj_t *prefix_label = create_label(box,
                                              prefix.c_str(),
                                              FIELD_TEXT_X,
                                              4,
                                              0,
                                              0xFFFFFF, &lv_font_montserrat_12);
        if (prefix_label) lv_obj_update_layout(prefix_label);
        const int measured_prefix_width = prefix_label ? lv_obj_get_width(prefix_label) : 0;
        const int prefix_width = std::min(measured_prefix_width, max_prefix_width);
        if (prefix_label && measured_prefix_width > max_prefix_width) {
            lv_obj_set_width(prefix_label, max_prefix_width);
            lv_label_set_long_mode(prefix_label, LV_LABEL_LONG_CLIP);
        }
        const int cursor_x = FIELD_TEXT_X + prefix_width + CURSOR_GAP;
        lv_obj_t *cursor_bar = lv_obj_create(box);
        if (cursor_bar) {
            lv_obj_set_size(cursor_bar, CURSOR_WIDTH, CURSOR_HEIGHT);
            lv_obj_set_pos(cursor_bar, cursor_x, 4);
            lv_obj_set_style_bg_color(cursor_bar, lv_color_hex(0x58A6FF), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cursor_bar, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(cursor_bar, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(cursor_bar, 0, LV_PART_MAIN);
            lv_obj_clear_flag(cursor_bar, LV_OBJ_FLAG_CLICKABLE);
        }
        const int suffix_x = cursor_x + CURSOR_WIDTH + CURSOR_GAP;
        create_label(box,
                     suffix.c_str(),
                     suffix_x,
                     4,
                     std::max(1, field_right - suffix_x),
                     0xFFFFFF, &lv_font_montserrat_12);
        (void)max_bytes;
    }

    void show_power_warning()
    {
        warning_active_ = true;
        lv_obj_clean(ComponensObj);
        lv_obj_t *overlay = lv_obj_create(ComponensObj);
        if (!overlay) return;
        lv_obj_set_size(overlay, SCREEN_W, SCREEN_H);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
        lv_obj_t *dialog = lv_obj_create(overlay);
        if (!dialog) return;
        lv_obj_set_size(dialog, 280, 92);
        lv_obj_center(dialog);
        lv_obj_set_style_radius(dialog, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(dialog, lv_color_hex(0xFFAA00), LV_PART_MAIN);
        lv_obj_set_style_bg_color(dialog, lv_color_hex(0x171717), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(dialog, 0, LV_PART_MAIN);
        create_label(dialog, "WiFi power is off", 12, 10, 250, 0xFFAA00, &lv_font_montserrat_14);
        create_label(dialog, "Turn on Power before continuing.", 12, 36, 250, 0xCCCCCC, &lv_font_montserrat_12);
        create_label(dialog, "OK", 246, 68, 28, 0x58A6FF, &lv_font_montserrat_12);
    }

    void append_text(const char *text)
    {
        if (saving_ || warning_active_ || !text || !text[0] || std::strpbrk(text, "\t\r\n")) return;
        std::string &value = current_value();
        const std::size_t limit = field_ == 0 ? MAX_SSID_BYTES : MAX_PASSWORD_BYTES;
        const std::size_t length = std::strlen(text);
        if (value.size() + length > limit) return;
        value.insert(current_cursor(), text, length);
        current_cursor() += length;
        error_.clear();
        render();
    }

    void move_cursor_left()
    {
        current_cursor() = previous_utf8_start(current_value(), current_cursor());
    }

    void move_cursor_right()
    {
        current_cursor() = next_utf8_end(current_value(), current_cursor());
    }

    void erase_last()
    {
        if (saving_ || current_cursor() == 0) return;
        const std::size_t start = previous_utf8_start(current_value(), current_cursor());
        current_value().erase(start, current_cursor() - start);
        current_cursor() = start;
    }

    void switch_field()
    {
        field_ = field_ == 0 ? 1 : 0;
        cursor_visible_ = true;
    }

    void connect()
    {
        if (saving_) return;
        if (ssid_.empty()) {
            error_ = "SSID is required";
            render();
            return;
        }
        saving_ = true;
        error_.clear();
        render();
        const std::string ssid = ssid_;
        const std::string password = password_;
        const auto dispatch = dispatch_;
        const std::weak_ptr<bool> lifetime = lifetime_;
        auto callback = [dispatch, lifetime, this](int result, std::string) mutable {
            std::lock_guard<std::mutex> lock(dispatch->mutex);
            if (dispatch->stopped || lifetime.expired()) return;
            dispatch->pending.emplace_back([lifetime, result, this]() {
                if (lifetime.expired()) return;
                saving_ = false;
                if (result != 0) {
                    error_ = "Hidden WiFi connection failed";
                    render();
                } else if (LeaveSelfPage) {
                    LeaveSelfPage();
                }
            });
        };
        if (!api_tasks_.start([ssid, password, callback = std::move(callback)]() mutable {
            try {
                if (password.empty())
                    cp0_signal_wifi_api({"ConnectHidden", ssid}, callback);
                else
                    cp0_signal_wifi_api({"ConnectHidden", ssid, password}, callback);
            } catch (...) {
                callback(-1, "WiFi service unavailable");
            }
        })) {
            saving_ = false;
            error_ = "WiFi request could not be scheduled";
            render();
        }
    }

    static void cursor_timer_cb(lv_timer_t *timer)
    {
        auto *self = timer ? static_cast<LvSettingWifiHiddenPage3 *>(lv_timer_get_user_data(timer)) : nullptr;
        if (!self || self->warning_active_) return;
        self->cursor_visible_ = !self->cursor_visible_;
        self->render();
        self->drain_callbacks();
    }

    void drain_callbacks()
    {
        std::deque<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(dispatch_->mutex);
            pending.swap(dispatch_->pending);
        }
        for (auto &callback : pending) if (callback) callback();
        api_tasks_.reap_finished();
    }

    static void keyboard_event_cb(lv_event_t *event)
    {
        if (!event) return;
        auto *self = static_cast<LvSettingWifiHiddenPage3 *>(lv_event_get_user_data(event));
        const auto *item = static_cast<const key_item *>(lv_event_get_param(event));
        if (!self || !item ||
            (item->key_state != KBD_KEY_PRESSED && item->key_state != KBD_KEY_REPEATED)) return;
        if (self->warning_active_) {
            if (item->key_code == KEY_ESC || item->key_code == KEY_ENTER ||
                item->key_code == KEY_KPENTER || item->key_code == KEY_LEFT ||
                item->key_code == KEY_RIGHT) {
                if (self->LeaveSelfPage) self->LeaveSelfPage();
            }
        } else if (item->key_code == KEY_ESC) {
            if (!self->saving_ && self->LeaveSelfPage) self->LeaveSelfPage();
        } else if (item->key_code == KEY_TAB ||
                   item->key_code == KEY_UP || item->key_code == KEY_DOWN) {
            self->switch_field();
            self->render();
        } else if (item->key_code == KEY_LEFT || item->key_code == KEY_RIGHT) {
            if (item->key_code == KEY_LEFT) self->move_cursor_left();
            else self->move_cursor_right();
            self->cursor_visible_ = true;
            self->render();
        } else if (item->key_code == KEY_BACKSPACE || item->key_code == KEY_DELETE) {
            self->erase_last();
            self->render();
        } else if (item->key_code == KEY_LEFTALT) {
            self->password_visible_ = !self->password_visible_;
            self->render();
        } else if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
            self->connect();
        } else if (item->utf8[0]) {
            self->append_text(item->utf8);
        }
        lv_event_stop_processing(event);
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        const uint32_t key = lv_event_get_key(event);
        if (warning_active_) {
            if (key == LV_KEY_ESC || key == LV_KEY_LEFT || key == LV_KEY_ENTER || key == LV_KEY_RIGHT)
                if (LeaveSelfPage) LeaveSelfPage();
        } else if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (!saving_ && LeaveSelfPage) LeaveSelfPage();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            connect();
        } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
            erase_last();
            render();
        } else if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
            switch_field();
            render();
        }
        lv_event_stop_processing(event);
    }

    NodeIter parent_node_;
    bool wifi_power_enabled_ = false;
    bool warning_active_ = false;
    bool saving_ = false;
    bool password_visible_ = false;
    bool cursor_visible_ = true;
    int field_ = 0;
    std::string ssid_;
    std::string password_;
    std::string error_;
    std::size_t ssid_cursor_ = 0;
    std::size_t password_cursor_ = 0;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
    lv_timer_t *cursor_timer_ = nullptr;
    cp0_keyboard_input_context_t previous_input_context_ = KBD_INPUT_CONTEXT_NAVIGATION;
    int previous_keypad_intercept_ = 0;
    bool input_mode_saved_ = true;
    Cp0BoundedTaskRegistry api_tasks_;
    std::shared_ptr<bool> lifetime_ = std::make_shared<bool>(true);
    std::shared_ptr<Cp0WifiHiddenUiDispatch> dispatch_ =
        std::make_shared<Cp0WifiHiddenUiDispatch>();
};
