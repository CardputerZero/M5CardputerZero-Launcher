#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "cp0_bounded_task_registry.hpp"
#include "cp0_font_service.hpp"
#include "cp0_lvgl_app.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "setting_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_componens.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

extern "C" {
extern const lv_image_dsc_t setting_red_up;
extern const lv_image_dsc_t setting_red_down;
extern const lv_image_dsc_t setting_right_arrow;
}

class LvSettingWifiScanPage3 : public DComponens::LvglComponensBase {
public:
    static constexpr int SCREEN_W     = 320;
    static constexpr int SCREEN_H     = 150;
    static constexpr int VISIBLE_ROWS = 5;
    static constexpr int ROW_H        = 22;
    static constexpr int ROW_Y        = 30;
    static constexpr int TITLE_W      = 300;
    static constexpr std::size_t MAX_PASSWORD_BYTES = 64;

    LvSettingWifiScanPage3() = default;

    LvSettingWifiScanPage3(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback)
        : parent_node_(parent_node), on_back(std::move(back_callback))
    {
        initialize(parent);
    }

    ~LvSettingWifiScanPage3() override
    {
        if (keyboard_root_ && keyboard_event_dsc_) {
            lv_obj_remove_event_dsc(keyboard_root_, keyboard_event_dsc_);
            keyboard_event_dsc_ = nullptr;
        }
        restore_text_input_mode();
        lifetime_token_.reset();
        stop_scan();
        stop_connection();
        scan_tasks_.join_all();
        connection_tasks_.join_all();
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
        title_ = nullptr;
        empty_ = nullptr;
        hint_ = nullptr;
        for (auto &row : rows_) row = {};
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
            std::bind(&LvSettingWifiScanPage3::key_event_cb,
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

        title_ = create_label(
            ComponensObj,
            "",
            8,
            2,
            0x58A6FF,
            cp0_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD));
        if (title_) {
            lv_obj_set_width(title_, TITLE_W);
            lv_label_set_long_mode(title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        }

        create_label(ComponensObj, "SSID", 8, 18, 0x888888, &lv_font_montserrat_10);
        create_label(ComponensObj, "Security", 180, 18, 0x888888, &lv_font_montserrat_10);
        create_label(ComponensObj, "Signal", 270, 18, 0x888888, &lv_font_montserrat_10);

        for (int index = 0; index < VISIBLE_ROWS; ++index) {
            auto &row = rows_[index];
            const int y = ROW_Y + index * ROW_H;

            row.background = lv_obj_create(ComponensObj);
            if (row.background) {
                lv_obj_set_size(row.background, SCREEN_W - 8, ROW_H - 2);
                lv_obj_set_pos(row.background, 4, y);
                lv_obj_set_style_radius(row.background, 2, LV_PART_MAIN);
                lv_obj_set_style_bg_color(row.background, lv_color_hex(0x1F3A5F), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(row.background, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(row.background, 0, LV_PART_MAIN);
                lv_obj_remove_flag(row.background, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_remove_flag(row.background, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(row.background, LV_OBJ_FLAG_HIDDEN);
            }

            row.ssid = create_label(
                ComponensObj, "", 8, y + 2, 0xCCCCCC, &lv_font_montserrat_12);
            row.security = create_label(
                ComponensObj, "", 180, y + 2, 0xCCCCCC, &lv_font_montserrat_10);
            row.signal = create_label(
                ComponensObj, "", 275, y + 2, 0xCCCCCC, &lv_font_montserrat_10);
            if (row.ssid) {
                lv_obj_set_width(row.ssid, 165);
                lv_label_set_long_mode(row.ssid, LV_LABEL_LONG_CLIP);
                lv_obj_add_flag(row.ssid, LV_OBJ_FLAG_HIDDEN);
            }
            if (row.security) lv_obj_add_flag(row.security, LV_OBJ_FLAG_HIDDEN);
            if (row.signal) lv_obj_add_flag(row.signal, LV_OBJ_FLAG_HIDDEN);
        }

        empty_ = create_label(
            ComponensObj, "", 8, 50, 0x666666, &lv_font_montserrat_12);
        hint_ = create_label(
            ComponensObj, "", 8, SCREEN_H - 14, 0x555555, &lv_font_montserrat_10);
        create_password_panel();

        refresh_status();
        render();
        start_scan();
    }

private:
    enum class View { List, Password, Connecting };
    enum class NetworkOperation { Connect, Forget };
    enum class ConnectionOrigin { OpenNetwork, SavedProfile, PasswordEntry };

    struct RowObjects {
        lv_obj_t *background = nullptr;
        lv_obj_t *ssid       = nullptr;
        lv_obj_t *security   = nullptr;
        lv_obj_t *signal     = nullptr;
        bool visible         = false;
        bool selected        = false;
        uint32_t color       = 0;
    };

    struct ScanState {
        std::atomic_bool stop{false};
        std::weak_ptr<bool> lifetime;
        LvSettingWifiScanPage3 *owner = nullptr;
    };

    struct ScanResult {
        std::shared_ptr<ScanState> state;
        std::array<cp0_wifi_ap_t, CP0_WIFI_AP_MAX> access_points{};
        int count = 0;
    };

    struct ConnectionState {
        std::atomic_bool stop{false};
        std::weak_ptr<bool> lifetime;
        LvSettingWifiScanPage3 *owner = nullptr;
        NetworkOperation operation = NetworkOperation::Connect;
        ConnectionOrigin origin = ConnectionOrigin::OpenNetwork;
        std::string ssid;
        std::string password;
        bool disconnect_active = false;
    };

    struct ConnectionResult {
        std::shared_ptr<ConnectionState> state;
        int result = CP0_WIFI_ERROR_SERVICE;
    };

    static lv_obj_t *create_label(lv_obj_t *parent,
                                  const char *text,
                                  int x,
                                  int y,
                                  uint32_t color,
                                  const lv_font_t *font)
    {
        if (!parent) return nullptr;
        lv_obj_t *label = lv_label_create(parent);
        if (!label) return nullptr;
        lv_label_set_text(label, text ? text : "");
        lv_obj_set_pos(label, x, y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        return label;
    }

    static const char *scan_error_message(int result)
    {
        switch (result) {
        case CP0_WIFI_ERROR_RADIO_OFF: return "WiFi is off. Turn on Power to scan";
        case CP0_WIFI_ERROR_TIMEOUT: return "WiFi scan timed out. Press R to retry";
        case CP0_WIFI_ERROR_SERVICE: return "WiFi service unavailable. Press R to retry";
        default: return "WiFi scan failed. Press R to retry";
        }
    }

    static const char *connection_error_message(int result)
    {
        switch (result) {
        case CP0_WIFI_ERROR_RADIO_OFF: return "WiFi is off";
        case CP0_WIFI_ERROR_AUTH: return "Incorrect password";
        case CP0_WIFI_ERROR_NOT_FOUND: return "Network is no longer available";
        case CP0_WIFI_ERROR_IP_CONFIG: return "Connected, but IP setup failed";
        case CP0_WIFI_ERROR_TIMEOUT: return "Network operation timed out";
        default: return "WiFi service unavailable";
        }
    }

    static bool is_open_security(const char *security)
    {
        if (!security || !security[0]) return true;
        std::string value(security);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return value == "OPEN" || value == "NONE";
    }

    static bool is_utf8_continuation(unsigned char value)
    {
        return (value & 0xC0u) == 0x80u;
    }

    static std::size_t previous_utf8_start(const std::string &value, std::size_t cursor)
    {
        if (cursor == 0 || cursor > value.size()) return 0;
        std::size_t start = cursor - 1;
        while (start > 0 && is_utf8_continuation(static_cast<unsigned char>(value[start])))
            --start;
        return start;
    }

    static std::size_t next_utf8_end(const std::string &value, std::size_t cursor)
    {
        if (cursor >= value.size()) return value.size();
        std::size_t end = cursor + 1;
        while (end < value.size() &&
               is_utf8_continuation(static_cast<unsigned char>(value[end])))
            ++end;
        return end;
    }

    static std::string masked_password(const std::string &password, bool visible)
    {
        if (visible) return password + "_";
        std::size_t codepoints = 0;
        for (unsigned char value : password)
            if (!is_utf8_continuation(value)) ++codepoints;
        return std::string(codepoints, '*') + "_";
    }

    void initialize(lv_obj_t *parent)
    {
        create_ui(parent);
    }

    void create_password_panel()
    {
        password_panel_ = lv_obj_create(ComponensObj);
        if (!password_panel_) return;
        lv_obj_set_size(password_panel_, SCREEN_W, SCREEN_H);
        lv_obj_set_pos(password_panel_, 0, 0);
        lv_obj_set_style_bg_color(password_panel_, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(password_panel_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(password_panel_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(password_panel_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(password_panel_, 0, LV_PART_MAIN);
        lv_obj_remove_flag(password_panel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(password_panel_, LV_OBJ_FLAG_SCROLLABLE);

        password_title_ = create_label(
            password_panel_,
            "WiFi password",
            8,
            8,
            0x58A6FF,
            cp0_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD));
        password_network_ = create_label(
            password_panel_, "", 8, 28, 0xCCCCCC, &lv_font_montserrat_10);
        password_value_ = create_label(
            password_panel_, "_", 8, 50, 0xFFFFFF, &lv_font_montserrat_16);
        password_status_ = create_label(
            password_panel_, "", 8, 78, 0xFF4444, &lv_font_montserrat_10);
        password_hint_ = create_label(
            password_panel_, "", 8, SCREEN_H - 14, 0x555555, &lv_font_montserrat_10);

        if (password_network_) lv_obj_set_width(password_network_, SCREEN_W - 16);
        if (password_value_) {
            lv_obj_set_width(password_value_, SCREEN_W - 16);
            lv_label_set_long_mode(password_value_, LV_LABEL_LONG_CLIP);
        }
        if (password_status_) lv_obj_set_width(password_status_, SCREEN_W - 16);
        if (password_hint_) lv_obj_set_width(password_hint_, SCREEN_W - 16);
        lv_obj_add_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);
    }

    void render_password_panel()
    {
        if (!password_panel_) return;
        lv_obj_remove_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);

        if (view_ == View::Connecting) {
            const bool forgetting = connection_state_ &&
                connection_state_->operation == NetworkOperation::Forget;
            if (password_title_)
                lv_label_set_text(password_title_, forgetting ? "Forgetting WiFi..."
                                                               : "Connecting...");
            if (password_network_) {
                const std::string text = "Network: " + password_ssid_;
                lv_label_set_text(password_network_, text.c_str());
            }
            if (password_value_)
                lv_label_set_text(password_value_,
                                  forgetting ? "Removing saved profile..." : "Please wait...");
            if (password_status_) {
                lv_label_set_text(password_status_, "");
                lv_obj_set_style_text_color(
                    password_status_, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
            }
            if (password_hint_) lv_label_set_text(password_hint_, "ESC:back");
            return;
        }

        if (password_title_) lv_label_set_text(password_title_, "WiFi password");
        if (password_network_) {
            std::string text = password_ssid_;
            if (!password_security_.empty()) {
                text += "  [";
                text += password_security_;
                text += "]";
            }
            lv_label_set_text(password_network_, text.c_str());
        }
        if (password_value_)
            lv_label_set_text(
                password_value_, masked_password(password_, password_visible_).c_str());
        if (password_status_) {
            lv_label_set_text(password_status_, password_error_.c_str());
            lv_obj_set_style_text_color(
                password_status_,
                lv_color_hex(password_error_.empty() ? 0x666666 : 0xFF4444),
                LV_PART_MAIN);
        }
        if (password_hint_) {
            lv_label_set_text(
                password_hint_,
                password_visible_
                    ? "ALT:hide  OK:connect  ESC:back"
                    : "ALT:show  OK:connect  ESC:back");
        }
    }

    void refresh_status()
    {
        cp0_wifi_status_t status{};
        const int result = cp0_wifi_status_read(&status);
        if (result == 0 && status.connected) {
            title_text_ = "Connected WiFi: ";
            title_text_ += status.ssid;
            title_text_ += "  ";
            title_text_ += status.ip;
        } else {
            title_text_ = "WiFi: Not connected";
        }
    }

    void render()
    {
        if (!ComponensObj) return;

        if (view_ != View::List) {
            render_password_panel();
            return;
        }
        if (password_panel_)
            lv_obj_add_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);

        if (title_) lv_label_set_text(title_, title_text_.c_str());
        if (empty_) {
            const std::string message = !scan_error_.empty()
                ? scan_error_
                : scanning_ ? "Scanning for WiFi networks..."
                            : "No networks found. Press R to rescan.";
            lv_label_set_text(empty_, message.c_str());
            if (access_points_.empty())
                lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
        }
        if (hint_) {
            lv_label_set_text(
                hint_,
                scanning_
                    ? "Scanning...  OK:connect  R:rescan  D:forget  ESC:back"
                    : "OK:connect  R:rescan  D:forget  ESC:back");
        }

        const int count = static_cast<int>(access_points_.size());
        int offset = selected_index_ - VISIBLE_ROWS / 2;
        offset = std::max(0, std::min(offset, std::max(0, count - VISIBLE_ROWS)));

        for (int visible = 0; visible < VISIBLE_ROWS; ++visible) {
            auto &row = rows_[visible];
            const int index = offset + visible;
            if (index < 0 || index >= count) {
                hide_row(row);
                continue;
            }

            const auto &access_point = access_points_[static_cast<std::size_t>(index)];
            const bool selected = index == selected_index_;
            const uint32_t color = access_point.in_use
                ? 0x58A6FF
                : selected ? 0xFFFFFF : 0xCCCCCC;
            std::string ssid = access_point.ssid;
            if (access_point.saved) ssid += " *";
            set_row_text(row.ssid, ssid);
            set_row_text(row.security,
                         access_point.security[0] ? access_point.security : "Open");
            char signal[16];
            std::snprintf(signal, sizeof(signal), "%d%%", std::clamp(access_point.signal, 0, 100));
            set_row_text(row.signal, signal);

            if (row.ssid)
                lv_obj_set_style_text_color(row.ssid, lv_color_hex(color), LV_PART_MAIN);
            if (row.security)
                lv_obj_set_style_text_color(row.security, lv_color_hex(color), LV_PART_MAIN);
            if (row.signal)
                lv_obj_set_style_text_color(row.signal, lv_color_hex(color), LV_PART_MAIN);
            if (row.background) {
                if (selected)
                    lv_obj_remove_flag(row.background, LV_OBJ_FLAG_HIDDEN);
                else
                    lv_obj_add_flag(row.background, LV_OBJ_FLAG_HIDDEN);
            }
            show_row(row);
            row.selected = selected;
            row.color = color;
        }
    }

    static void set_row_text(lv_obj_t *label, const std::string &text)
    {
        if (label) lv_label_set_text(label, text.c_str());
    }

    static void set_row_text(lv_obj_t *label, const char *text)
    {
        if (label) lv_label_set_text(label, text ? text : "");
    }

    static void hide_row(RowObjects &row)
    {
        if (row.background) lv_obj_add_flag(row.background, LV_OBJ_FLAG_HIDDEN);
        if (row.ssid) lv_obj_add_flag(row.ssid, LV_OBJ_FLAG_HIDDEN);
        if (row.security) lv_obj_add_flag(row.security, LV_OBJ_FLAG_HIDDEN);
        if (row.signal) lv_obj_add_flag(row.signal, LV_OBJ_FLAG_HIDDEN);
        row.visible = false;
        row.selected = false;
    }

    static void show_row(RowObjects &row)
    {
        if (row.ssid) lv_obj_remove_flag(row.ssid, LV_OBJ_FLAG_HIDDEN);
        if (row.security) lv_obj_remove_flag(row.security, LV_OBJ_FLAG_HIDDEN);
        if (row.signal) lv_obj_remove_flag(row.signal, LV_OBJ_FLAG_HIDDEN);
        row.visible = true;
    }

    void enter_text_input_mode()
    {
        if (!keyboard_mode_saved_) {
            previous_input_context_ = cp0_keyboard_get_input_context();
            previous_keypad_intercept_ = cp0_keyboard_get_lvgl_keypad_intercept();
            keyboard_mode_saved_ = true;
        }
        cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_TEXT);
        cp0_keyboard_set_lvgl_keypad_intercept(1);
    }

    void restore_text_input_mode()
    {
        if (!keyboard_mode_saved_) return;
        cp0_keyboard_set_input_context(previous_input_context_);
        cp0_keyboard_set_lvgl_keypad_intercept(previous_keypad_intercept_);
        keyboard_mode_saved_ = false;
    }

    void clear_password()
    {
        volatile char *bytes = password_.empty() ? nullptr : password_.data();
        for (std::size_t index = 0; bytes && index < password_.size(); ++index)
            bytes[index] = '\0';
        password_.clear();
        password_cursor_byte_ = 0;
    }

    bool append_password_text(const char *text)
    {
        if (!text || !text[0]) return false;
        const std::string value(text);
        if (value.empty() || password_.size() + value.size() > MAX_PASSWORD_BYTES)
            return false;
        for (unsigned char byte : value) {
            if (byte < 0x20u || byte == 0x7Fu) return false;
        }
        password_.insert(password_cursor_byte_, value);
        password_cursor_byte_ += value.size();
        return true;
    }

    bool erase_password_last()
    {
        if (password_cursor_byte_ == 0) return false;
        const std::size_t start = previous_utf8_start(password_, password_cursor_byte_);
        volatile char *bytes = password_.data();
        for (std::size_t index = start; index < password_cursor_byte_; ++index)
            bytes[index] = '\0';
        password_.erase(start, password_cursor_byte_ - start);
        password_cursor_byte_ = start;
        return true;
    }

    bool move_password_cursor_left()
    {
        const std::size_t next = previous_utf8_start(password_, password_cursor_byte_);
        if (next == password_cursor_byte_) return false;
        password_cursor_byte_ = next;
        return true;
    }

    bool move_password_cursor_right()
    {
        const std::size_t next = next_utf8_end(password_, password_cursor_byte_);
        if (next == password_cursor_byte_) return false;
        password_cursor_byte_ = next;
        return true;
    }

    std::string password_validation_error() const
    {
        if (password_.empty()) return "Password required";

        std::string security = password_security_;
        std::transform(security.begin(), security.end(), security.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        if (security.find("802.1X") != std::string::npos ||
            security.find("EAP") != std::string::npos ||
            security.find("ENTERPRISE") != std::string::npos)
            return "Enterprise WiFi is not supported";
        if (security.find("WPA") == std::string::npos) return {};
        if (password_.size() >= 8 && password_.size() <= 63) return {};
        if (password_.size() == 64 &&
            std::all_of(password_.begin(), password_.end(), [](unsigned char ch) {
                return std::isxdigit(ch) != 0;
            }))
            return {};
        return "WPA password: 8-63 chars or 64 hex";
    }

    void show_password_prompt(const std::string &ssid,
                              const std::string &security,
                              const std::string &error = {})
    {
        stop_scan();
        stop_connection();
        view_ = View::Password;
        password_ssid_ = ssid;
        password_security_ = security;
        password_error_ = error;
        password_visible_ = false;
        clear_password();
        enter_text_input_mode();
        render();
    }

    void leave_password_prompt()
    {
        restore_text_input_mode();
        clear_password();
        password_ssid_.clear();
        password_security_.clear();
        password_error_.clear();
        password_visible_ = false;
        view_ = View::List;
    }

    void submit_password()
    {
        if (view_ != View::Password || connection_pending_) return;
        const std::string error = password_validation_error();
        if (!error.empty()) {
            password_error_ = error;
            render();
            return;
        }
        start_connection(password_ssid_, password_, ConnectionOrigin::PasswordEntry);
    }

    bool start_network_operation(NetworkOperation operation,
                                 const std::string &ssid,
                                 const std::string &password,
                                 const std::string &security,
                                 ConnectionOrigin origin,
                                 bool disconnect_active = false)
    {
        if (ssid.empty() || connection_pending_) return false;

        auto state = std::make_shared<ConnectionState>();
        state->lifetime = lifetime_token_;
        state->owner = this;
        state->operation = operation;
        state->origin = origin;
        state->ssid = ssid;
        state->password = password;
        state->security = security;
        state->disconnect_active = disconnect_active;
        connection_state_ = state;
        connection_pending_ = true;
        password_ssid_ = ssid;
        password_security_ = security;
        restore_text_input_mode();
        clear_password();
        view_ = View::Connecting;
        render();

        connection_tasks_.reap_finished();
        if (!connection_tasks_.start([state] {
                int result = CP0_WIFI_ERROR_SERVICE;
                try {
                    if (!state->stop.load(std::memory_order_acquire)) {
                        if (state->operation == NetworkOperation::Forget) {
                            result = cp0_wifi_profile_forget(state->ssid.c_str());
                            if (result == 0 && state->disconnect_active)
                                result = cp0_wifi_disconnect_active();
                        } else {
                            const char *password = state->password.empty()
                                ? nullptr
                                : state->password.c_str();
                            result = cp0_wifi_connect(state->ssid.c_str(), password);
                        }
                    }
                } catch (...) {
                    result = CP0_WIFI_ERROR_SERVICE;
                }
                if (state->stop.load(std::memory_order_acquire)) return;

                auto *queued = new (std::nothrow) ConnectionResult{state, result};
                if (!queued) return;
                lv_lock();
                const lv_result_t status = lv_async_call(connection_result_cb, queued);
                lv_unlock();
                if (status != LV_RESULT_OK) delete queued;
            })) {
            connection_state_.reset();
            connection_pending_ = false;
            view_ = View::List;
            scan_error_ = "Unable to start WiFi operation";
            render();
            return false;
        }
        return true;
    }

    bool start_connection(const std::string &ssid,
                          const std::string &password,
                          ConnectionOrigin origin,
                          const std::string &security = {})
    {
        stop_scan();
        return start_network_operation(
            NetworkOperation::Connect, ssid, password, security, origin);
    }

    void stop_connection()
    {
        if (connection_state_)
            connection_state_->stop.store(true, std::memory_order_release);
        connection_state_.reset();
        connection_pending_ = false;
    }

    static void connection_result_cb(void *user_data) noexcept
    {
        std::unique_ptr<ConnectionResult> result(static_cast<ConnectionResult *>(user_data));
        if (!result || !result->state) return;
        auto lifetime = result->state->lifetime.lock();
        if (!lifetime || result->state->stop.load(std::memory_order_acquire)) return;

        LvSettingWifiScanPage3 *self = result->state->owner;
        if (!self || self->connection_state_.get() != result->state.get()) return;
        self->connection_tasks_.reap_finished();
        const auto state = result->state;
        self->connection_state_.reset();
        self->connection_pending_ = false;

        if (state->operation == NetworkOperation::Forget) {
            self->view_ = View::List;
            self->password_ssid_.clear();
            self->password_security_.clear();
            self->scan_error_ = result->result == 0
                ? std::string()
                : connection_error_message(result->result);
            self->refresh_status();
            self->render();
            if (result->result == 0) self->start_scan();
            return;
        }

        if (result->result == 0) {
            self->leave_password_prompt();
            self->refresh_status();
            self->render();
            self->start_scan();
            return;
        }

        const std::string error = connection_error_message(result->result);
        if (state->origin == ConnectionOrigin::SavedProfile ||
            state->origin == ConnectionOrigin::PasswordEntry) {
            self->show_password_prompt(state->ssid, state->security.empty() ? "WPA" : state->security,
                                        error);
            return;
        }

        self->view_ = View::List;
        self->scan_error_ = error;
        self->render();
    }

    void activate_selected()
    {
        if (view_ != View::List || connection_pending_ || access_points_.empty()) return;
        if (selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(access_points_.size()))
            return;

        const cp0_wifi_ap_t access_point =
            access_points_[static_cast<std::size_t>(selected_index_)];
        if (access_point.in_use || access_point.ssid[0] == '\0') return;
        if (is_open_security(access_point.security)) {
            start_connection(access_point.ssid, {}, ConnectionOrigin::OpenNetwork,
                             access_point.security);
        } else if (access_point.saved) {
            start_connection(access_point.ssid, {}, ConnectionOrigin::SavedProfile,
                             access_point.security);
        } else {
            show_password_prompt(access_point.ssid, access_point.security);
        }
    }

    void forget_selected()
    {
        if (view_ != View::List || connection_pending_ || scanning_ || access_points_.empty())
            return;
        if (selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(access_points_.size()))
            return;
        const cp0_wifi_ap_t &access_point =
            access_points_[static_cast<std::size_t>(selected_index_)];
        if (!access_point.saved || access_point.ssid[0] == '\0') return;
        stop_scan();
        start_network_operation(
            NetworkOperation::Forget,
            access_point.ssid,
            {},
            access_point.security,
            ConnectionOrigin::OpenNetwork,
            access_point.in_use != 0);
    }

    static void keyboard_event_cb(lv_event_t *event)
    {
        if (!event) return;
        auto *self = static_cast<LvSettingWifiScanPage3 *>(lv_event_get_user_data(event));
        auto *item = static_cast<const key_item *>(lv_event_get_param(event));
        if (!self || !item || item->key_state != KBD_KEY_PRESSED) return;
        if (item->key_code == KEY_R || item->semantic_key == KEY_R)
            self->start_scan();
    }

    void start_scan()
    {
        if (scanning_) {
            scan_restart_pending_ = true;
            return;
        }

        selected_ssid_before_scan_.clear();
        if (selected_index_ >= 0 &&
            selected_index_ < static_cast<int>(access_points_.size())) {
            selected_ssid_before_scan_ =
                access_points_[static_cast<std::size_t>(selected_index_)].ssid;
        }

        stop_scan();
        scan_error_.clear();
        scanning_ = true;
        render();

        auto state = std::make_shared<ScanState>();
        state->lifetime = lifetime_token_;
        state->owner = this;
        scan_state_ = state;

        if (!scan_tasks_.start([state] {
                auto result = std::make_unique<ScanResult>();
                result->state = state;
                try {
                    if (!state->stop.load(std::memory_order_acquire)) {
                        result->count = cp0_wifi_radio_enabled() != 0
                            ? cp0_wifi_scan(result->access_points.data(), CP0_WIFI_AP_MAX)
                            : CP0_WIFI_ERROR_RADIO_OFF;
                    }
                } catch (...) {
                    result->count = CP0_WIFI_ERROR_SERVICE;
                }
                if (state->stop.load(std::memory_order_acquire)) return;

                ScanResult *queued = result.release();
                lv_lock();
                const lv_result_t status = lv_async_call(scan_result_cb, queued);
                lv_unlock();
                if (status != LV_RESULT_OK) delete queued;
            })) {
            scanning_ = false;
            scan_error_ = "Unable to start WiFi scan";
            render();
        }
    }

    void stop_scan()
    {
        if (scan_state_) scan_state_->stop.store(true, std::memory_order_release);
        scan_state_.reset();
        scanning_ = false;
        scan_restart_pending_ = false;
    }

    static void scan_result_cb(void *user_data) noexcept
    {
        std::unique_ptr<ScanResult> result(static_cast<ScanResult *>(user_data));
        if (!result || !result->state) return;
        auto lifetime = result->state->lifetime.lock();
        if (!lifetime || result->state->stop.load(std::memory_order_acquire)) return;

        LvSettingWifiScanPage3 *self = result->state->owner;
        if (!self || self->scan_state_.get() != result->state.get()) return;
        self->scan_tasks_.reap_finished();
        self->scanning_ = false;
        self->apply_scan_result(*result);
        if (self->scan_restart_pending_) {
            self->scan_restart_pending_ = false;
            self->start_scan();
        }
    }

    void apply_scan_result(const ScanResult &result)
    {
        const int count = std::clamp(result.count, 0, CP0_WIFI_AP_MAX);
        if (result.count < 0) {
            access_points_.clear();
            selected_index_ = 0;
            selected_ssid_before_scan_.clear();
            scan_error_ = scan_error_message(result.count);
            render();
            return;
        }

        access_points_.assign(result.access_points.begin(),
                              result.access_points.begin() + count);
        if (access_points_.empty()) {
            selected_index_ = 0;
        } else {
            selected_index_ = std::clamp(
                selected_index_, 0, static_cast<int>(access_points_.size()) - 1);
            if (!selected_ssid_before_scan_.empty()) {
                const auto selected = std::find_if(
                    access_points_.begin(),
                    access_points_.end(),
                    [this](const cp0_wifi_ap_t &access_point) {
                        return selected_ssid_before_scan_ == access_point.ssid;
                    });
                if (selected != access_points_.end()) {
                    selected_index_ = static_cast<int>(
                        std::distance(access_points_.begin(), selected));
                }
            }
        }
        selected_ssid_before_scan_.clear();
        scan_error_.clear();
        refresh_status();
        render();
    }

    bool move_selection(int delta)
    {
        if (scanning_ || access_points_.empty() || delta == 0) return false;
        const int next = std::clamp(
            selected_index_ + delta,
            0,
            static_cast<int>(access_points_.size()) - 1);
        if (next == selected_index_) return false;
        selected_index_ = next;
        return true;
    }

    void key_event_cb(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            stop_scan();
            if (on_back) on_back();
        } else if (key == LV_KEY_UP) {
            if (move_selection(-1)) render();
        } else if (key == LV_KEY_DOWN) {
            if (move_selection(1)) render();
        } else if (key == LV_KEY_NEXT || key == 'r' || key == 'R') {
            start_scan();
        }
        lv_event_stop_processing(event);
    }

    std::shared_ptr<bool> lifetime_token_ = std::make_shared<bool>(true);
    Cp0BoundedTaskRegistry scan_tasks_;
    std::shared_ptr<ScanState> scan_state_;
    NodeIter parent_node_;
    std::function<void()> on_back;
    std::array<RowObjects, VISIBLE_ROWS> rows_{};
    std::vector<cp0_wifi_ap_t> access_points_;
    int selected_index_ = 0;
    bool scanning_ = false;
    bool scan_restart_pending_ = false;
    std::string selected_ssid_before_scan_;
    std::string title_text_;
    std::string scan_error_;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *empty_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
};

class LvSettingValuePage3Base : public DComponens::LvglComponensBase {
public:
    static constexpr int SCREEN_W          = 320;
    static constexpr int SCREEN_H          = 150;
    static constexpr int ROW_H             = 21;
    static constexpr int CENTER_ROW        = 3;
    static constexpr int EDGE_PADDING      = ROW_H * CENTER_ROW;
    static constexpr int BAR_X             = 4;
    static constexpr int BAR_Y             = 66;
    static constexpr int BAR_W             = 312;
    static constexpr int BAR_H             = 22;
    static constexpr int TITLE_CENTER_X    = 60;
    static constexpr int TITLE_BOX_W       = 84;
    static constexpr int VALUE_LIST_X      = 100;
    static constexpr int VALUE_LIST_W      = 120;
    static constexpr int VALUE_CENTER_X    = VALUE_LIST_W / 2;
    static constexpr int VALUE_BOX_X       = 16;
    static constexpr int VALUE_BOX_W       = 88;
    static constexpr int RIGHT_ARROW_SCALE = 224;

    int32_t selected_index = 0;
    std::function<void()> on_back = nullptr;

    LvSettingValuePage3Base() = default;

    ~LvSettingValuePage3Base() override
    {
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
        selection_bg_ = nullptr;
        value_list_   = nullptr;
        title_label_  = nullptr;
        right_arrow_  = nullptr;
        arrow_up_     = nullptr;
        arrow_down_   = nullptr;
        hint_         = nullptr;
    }

    static void style_value_label(lv_obj_t *label, int distance)
    {
        if (!label) return;

        int font_size  = 10;
        int opa        = 130;
        uint32_t color = 0x555555;
        if (distance == 0) {
            font_size = 16;
            opa       = 255;
            color     = 0xFFFFFF;
        } else if (distance == 1) {
            font_size = 12;
            opa       = 220;
            color     = 0xAAAAAA;
        } else if (distance == 2) {
            font_size = 12;
            opa       = 170;
            color     = 0x777777;
        }

        lv_obj_set_style_text_font(
            label,
            cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD),
            LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_opa(label, opa, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(label);

        const bool focused      = distance == 0;
        const int natural_width = lv_obj_get_width(label);
        if (natural_width > VALUE_BOX_W) {
            lv_obj_set_width(label, VALUE_BOX_W);
            lv_label_set_long_mode(
                label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, VALUE_BOX_X);
        } else {
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_update_layout(label);
            lv_obj_set_x(label, VALUE_CENTER_X - lv_obj_get_width(label) / 2);
        }

        const int label_y = (ROW_H - lv_obj_get_height(label)) / 2;
        lv_obj_set_y(label, std::max(0, label_y));
    }

    void update_title_position()
    {
        if (!title_label_) return;

        lv_obj_set_width(title_label_, LV_SIZE_CONTENT);
        lv_label_set_long_mode(title_label_, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(title_label_);

        if (lv_obj_get_width(title_label_) > TITLE_BOX_W) {
            lv_obj_set_width(title_label_, TITLE_BOX_W);
            lv_label_set_long_mode(title_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_x(title_label_, TITLE_CENTER_X - TITLE_BOX_W / 2);
        } else {
            lv_obj_set_x(title_label_,
                         TITLE_CENTER_X - lv_obj_get_width(title_label_) / 2);
        }

        const int label_y = BAR_Y + (BAR_H - lv_obj_get_height(title_label_)) / 2;
        lv_obj_set_y(title_label_, std::max(0, label_y));
    }

    void update_value_styles()
    {
        int index = 0;
        for (lv_obj_t *row : value_rows_) {
            if (!row) {
                ++index;
                continue;
            }
            lv_obj_t *label = lv_obj_get_child(row, 0);
            style_value_label(label, std::abs(index - selected_index));
            ++index;
        }
        update_right_arrow_position();
    }

    void update_right_arrow_position()
    {
        if (!right_arrow_ || !title_label_ || item_count_ == 0) return;

        lv_obj_t *row = row_at(selected_index);
        lv_obj_t *label = row ? lv_obj_get_child(row, 0) : nullptr;
        if (!label) return;

        lv_obj_update_layout(title_label_);
        lv_obj_update_layout(label);
        lv_obj_update_layout(right_arrow_);

        const int title_right = lv_obj_get_x(title_label_) + lv_obj_get_width(title_label_);
        const int value_left = VALUE_LIST_X + lv_obj_get_x(label);
        const int arrow_width = lv_obj_get_width(right_arrow_);
        const int arrow_x = std::max(title_right + 4, value_left - 4 - arrow_width);
        const int arrow_y = BAR_Y + (BAR_H - lv_obj_get_height(right_arrow_)) / 2;
        lv_obj_set_pos(right_arrow_, arrow_x, std::max(0, arrow_y));
        lv_obj_move_to_index(right_arrow_, 1);
    }

    void update_arrow_visibility()
    {
        if (arrow_up_) {
            if (selected_index > 0)
                lv_obj_remove_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
        }
        if (arrow_down_) {
            if (selected_index + 1 < static_cast<int32_t>(item_count_))
                lv_obj_remove_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void scroll_to_selected(bool animated)
    {
        if (!value_list_ || item_count_ == 0) return;

        lv_obj_t *row = row_at(selected_index);
        if (!row) return;

        lv_obj_scroll_to_view(row, animated ? LV_ANIM_ON : LV_ANIM_OFF);
        update_value_styles();
        update_arrow_visibility();
    }

    void select(int index)
    {
        if (item_count_ == 0) return;
        selected_index = std::clamp(index, 0, static_cast<int>(item_count_ - 1));
        scroll_to_selected(false);
    }

    lv_obj_t *row_at(int index) const
    {
        if (index < 0 || index >= static_cast<int>(value_rows_.size())) return nullptr;
        auto row = std::next(value_rows_.begin(), index);
        return row == value_rows_.end() ? nullptr : *row;
    }

protected:
    LvSettingValuePage3Base(const NodeIter &parent_node,
                            std::function<void()> back_callback)
        : parent_node_(parent_node), on_back(std::move(back_callback))
    {
    }

    void initialize(lv_obj_t *parent)
    {
        create_ui(parent);
    }

    const NodeIter &parent_node() const
    {
        return parent_node_;
    }

    virtual int initial_selection() const = 0;

    virtual void activate_selected()
    {
        if (item_count_ == 0) return;

        auto selected_node = std::next(parent_node_.begin(), selected_index);
        if (selected_node->Componens_api)
            selected_node->Componens_api(SettingApiActivate, this);
        if (on_back) on_back();
    }

    void key_event_cb(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (on_back) on_back();
            lv_event_stop_processing(event);
            return;
        }

        if (key == LV_KEY_UP) {
            if (selected_index > 0) {
                --selected_index;
                scroll_to_selected(true);
            }
        } else if (key == LV_KEY_DOWN) {
            if (selected_index + 1 < static_cast<int32_t>(item_count_)) {
                ++selected_index;
                scroll_to_selected(true);
            }
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            activate_selected();
        }

        lv_event_stop_processing(event);
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
            std::bind(&LvSettingValuePage3Base::key_event_cb, this, std::placeholders::_1));

        selection_bg_ = lv_obj_create(ComponensObj);
        if (selection_bg_) {
            lv_obj_set_size(selection_bg_, BAR_W, BAR_H);
            lv_obj_set_pos(selection_bg_, BAR_X, BAR_Y);
            lv_obj_set_style_bg_color(selection_bg_, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(selection_bg_, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(selection_bg_, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(selection_bg_, 0, LV_PART_MAIN);
            lv_obj_remove_flag(selection_bg_, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(selection_bg_, LV_OBJ_FLAG_SCROLLABLE);
        }

        value_list_ = lv_obj_create(ComponensObj);
        if (value_list_) {
            lv_obj_set_size(value_list_, VALUE_LIST_W, SCREEN_H);
            lv_obj_set_pos(value_list_, VALUE_LIST_X, 0);
            lv_obj_set_style_bg_opa(value_list_, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_top(value_list_, EDGE_PADDING, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(value_list_, EDGE_PADDING, LV_PART_MAIN);
            lv_obj_set_style_pad_row(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_clip_corner(value_list_, true, LV_PART_MAIN);
            lv_obj_set_flex_flow(value_list_, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_scroll_dir(value_list_, LV_DIR_VER);
            lv_obj_set_scroll_snap_y(value_list_, LV_SCROLL_SNAP_CENTER);
            lv_obj_set_scrollbar_mode(value_list_, LV_SCROLLBAR_MODE_OFF);
            lv_obj_remove_flag(value_list_, LV_OBJ_FLAG_SCROLL_WITH_ARROW);

            for (auto it = parent_node_.begin(); it != parent_node_.end(); ++it) {
                lv_obj_t *row = lv_obj_create(value_list_);
                if (!row) continue;
                value_rows_.push_back(row);
                lv_obj_set_size(row, VALUE_LIST_W, ROW_H);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                lv_obj_t *label = lv_label_create(row);
                if (label) lv_label_set_text(label, it->label.c_str());
            }
            item_count_ = static_cast<uint32_t>(value_rows_.size());
        }

        title_label_ = lv_label_create(ComponensObj);
        if (title_label_) {
            lv_label_set_text(title_label_, parent_node_->label.c_str());
            lv_obj_set_style_text_font(
                title_label_,
                cp0_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
                LV_PART_MAIN);
            lv_obj_set_style_text_color(title_label_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            update_title_position();
        }

        right_arrow_ = lv_img_create(ComponensObj);
        if (right_arrow_) {
            lv_img_set_src(right_arrow_, &setting_right_arrow);
            lv_image_set_pivot(right_arrow_, 0, 0);
            lv_image_set_scale(right_arrow_, RIGHT_ARROW_SCALE);
            lv_obj_update_layout(right_arrow_);
            lv_obj_set_pos(right_arrow_, VALUE_LIST_X - lv_obj_get_width(right_arrow_) - 4,
                           BAR_Y + (BAR_H - lv_obj_get_height(right_arrow_)) / 2);
        }

        arrow_up_ = lv_img_create(ComponensObj);
        if (arrow_up_) {
            lv_img_set_src(arrow_up_, &setting_red_up);
            lv_obj_update_layout(arrow_up_);
            lv_obj_set_pos(arrow_up_,
                           VALUE_LIST_X + VALUE_CENTER_X - lv_obj_get_width(arrow_up_) / 2,
                           2);
        }

        arrow_down_ = lv_img_create(ComponensObj);
        if (arrow_down_) {
            lv_img_set_src(arrow_down_, &setting_red_down);
            lv_obj_update_layout(arrow_down_);
            lv_obj_set_pos(arrow_down_,
                           VALUE_LIST_X + VALUE_CENTER_X - lv_obj_get_width(arrow_down_) / 2,
                           SCREEN_H - lv_obj_get_height(arrow_down_) - 4);
        }

        hint_ = lv_label_create(ComponensObj);
        if (hint_) {
            lv_label_set_text(hint_, "ok:set");
            lv_obj_set_style_text_color(hint_, lv_color_hex(0x00CC66), LV_PART_MAIN);
            lv_obj_set_style_text_font(
                hint_,
                cp0_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
                LV_PART_MAIN);
            lv_obj_update_layout(hint_);
            lv_obj_set_pos(hint_, SCREEN_W - 6 - lv_obj_get_width(hint_),
                           BAR_Y + (BAR_H - lv_obj_get_height(hint_)) / 2);
        }

        if (item_count_ > 0) {
            lv_obj_update_layout(value_list_);
            select(initial_selection());
        }
        else update_arrow_visibility();
    }

private:
    NodeIter parent_node_;
    uint32_t item_count_ = 0;
    std::list<lv_obj_t *> value_rows_;
    lv_obj_t *selection_bg_ = nullptr;
    lv_obj_t *value_list_   = nullptr;
    lv_obj_t *title_label_  = nullptr;
    lv_obj_t *right_arrow_  = nullptr;
    lv_obj_t *arrow_up_     = nullptr;
    lv_obj_t *arrow_down_   = nullptr;
    lv_obj_t *hint_         = nullptr;
};

class LvSettingRollerPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRollerPage3() = default;

    LvSettingRollerPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingRollerPage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

private:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingBrightnessPage3 : public LvSettingValuePage3Base {
public:
    LvSettingBrightnessPage3() = default;

    LvSettingBrightnessPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingBrightnessPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingDarkTimePage3 : public LvSettingValuePage3Base {
public:
    LvSettingDarkTimePage3() = default;

    LvSettingDarkTimePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingDarkTimePage3(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 2;
    }
};

class LvSettingVolumePage3 : public LvSettingValuePage3Base {
public:
    LvSettingVolumePage3() = default;

    LvSettingVolumePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingVolumePage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingResolutionPage3 : public LvSettingValuePage3Base {
public:
    LvSettingResolutionPage3() = default;

    LvSettingResolutionPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingResolutionPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingBQCalibratePage3 : public LvSettingValuePage3Base {
public:
    LvSettingBQCalibratePage3() = default;

    LvSettingBQCalibratePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingBQCalibratePage3(lv_obj_t *parent,
                              const NodeIter &parent_node,
                              std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingRtcPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRtcPage3() = default;

    LvSettingRtcPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingRtcPage3(lv_obj_t *parent,
                      const NodeIter &parent_node,
                      std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        const std::string &label = parent_node()->label;
        const std::time_t now = std::time(nullptr);
        const std::tm *local = std::localtime(&now);
        if (!local) return 0;
        if (label == "Year") return local->tm_year + 1900 - 2000;
        if (label == "Month") return local->tm_mon;
        if (label == "Day") return local->tm_mday - 1;
        if (label == "Hour") return local->tm_hour;
        if (label == "Minute") return local->tm_min;
        if (label == "Second") return local->tm_sec;
        return 0;
    }
};

class LvSettingConfirmPage3 : public LvSettingValuePage3Base {
public:
    LvSettingConfirmPage3() = default;

    LvSettingConfirmPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingConfirmPage3(lv_obj_t *parent,
                          const NodeIter &parent_node,
                          std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 1;
    }
};

class LvSettingAdbGuidePage3 : public DComponens::LvglComponensBase {
public:
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 150;

    LvSettingAdbGuidePage3() = default;

    LvSettingAdbGuidePage3(lv_obj_t *parent,
                           const NodeIter &page_node,
                           std::function<void()> back_callback)
        : page_node_(page_node), on_back(std::move(back_callback))
    {
        create_ui(parent);
    }

    ~LvSettingAdbGuidePage3() override
    {
        stop_animation();
        if (ComponensObj) {
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
            std::bind(&LvSettingAdbGuidePage3::key_event_cb, this, std::placeholders::_1));

        const lv_font_t *title_font =
            cp0_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD);
        const lv_font_t *text_font = &lv_font_montserrat_10;
        add_label(8, 2, "Enable ADB - switch USB to device", 0xECECEC,
                  title_font ? title_font : &lv_font_montserrat_12);
        add_chip(86, 24, 146, 50, 0x282A30, 0x5A5C64, 6, 2);
        add_label(120, 28, "CardputerZero", 0x9A9AA0, text_font);
        add_chip(218, 30, 12, 12, 0x101012, 0x5A5C64, 3, 2);
        add_chip(228, 32, 22, 8, 0xCDCDD2, 0xCDCDD2, 2, 0);
        add_chip(250, 34, 60, 4, 0x6A6C72, 0x6A6C72, 2, 0);
        add_label(232, 42, "USB-C", 0x46DC87, text_font);
        add_chip(24, 28, 32, 44, 0x1A1A1C, 0x5A5C64, 6, 2);
        add_chip(33, 33, 14, 34, 0x0E0E10, 0x0E0E10, 4, 0);
        add_label(26, 14, "USB", 0x46DC87, text_font);
        add_label(28, 72, "HUB", 0xEB5F5F, text_font);

        knob_ = add_chip(32, 54, 16, 10, 0x46DC87, 0x2A6F49, 3, 1);
        if (knob_) {
            lv_anim_t animation;
            lv_anim_init(&animation);
            lv_anim_set_var(&animation, knob_);
            lv_anim_set_values(&animation, 54, 34);
            lv_anim_set_time(&animation, 650);
            lv_anim_set_playback_time(&animation, 650);
            lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
            lv_anim_set_exec_cb(
                &animation,
                [](void *object, int32_t value) {
                    if (object) lv_obj_set_y(static_cast<lv_obj_t *>(object), value);
                });
            lv_anim_start(&animation);
        }

        add_label(8, 80, "1  Slide LEFT switch  HUB -> USB", 0xECECEC, text_font);
        add_label(8, 95, "2  USB hub & peripherals turn OFF", 0xF0C850, text_font);
        add_label(8, 110, "3  Cable -> top-right USB-C port", 0x46DC87, text_font);
        confirm_label_ = add_label(8, SCREEN_H - 16,
                                   "OK: reboot now     ESC: later", 0x9A9AA0, text_font);
    }

private:
    static lv_obj_t *create_chip(lv_obj_t *parent,
                                 int pos_x,
                                 int pos_y,
                                 int width,
                                 int height,
                                 uint32_t background,
                                 uint32_t border,
                                 int radius,
                                 int border_width)
    {
        if (!parent) return nullptr;
        lv_obj_t *chip = lv_obj_create(parent);
        if (!chip) return nullptr;
        lv_obj_set_pos(chip, pos_x, pos_y);
        lv_obj_set_size(chip, width, height);
        lv_obj_set_style_radius(chip, radius, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chip, lv_color_hex(background), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(chip, lv_color_hex(border), LV_PART_MAIN);
        lv_obj_set_style_border_width(chip, border_width, LV_PART_MAIN);
        lv_obj_set_style_pad_all(chip, 0, LV_PART_MAIN);
        lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        return chip;
    }

    static lv_obj_t *create_label(lv_obj_t *parent,
                                  int pos_x,
                                  int pos_y,
                                  const char *text,
                                  uint32_t color,
                                  const lv_font_t *font)
    {
        if (!parent) return nullptr;
        lv_obj_t *label = lv_label_create(parent);
        if (!label) return nullptr;
        lv_label_set_text(label, text);
        lv_obj_set_pos(label, pos_x, pos_y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        return label;
    }

    lv_obj_t *add_chip(int pos_x,
                       int pos_y,
                       int width,
                       int height,
                       uint32_t background,
                       uint32_t border,
                       int radius,
                       int border_width)
    {
        return create_chip(ComponensObj, pos_x, pos_y, width, height, background, border,
                           radius, border_width);
    }

    lv_obj_t *add_label(int pos_x,
                        int pos_y,
                        const char *text,
                        uint32_t color,
                        const lv_font_t *font)
    {
        return create_label(ComponensObj, pos_x, pos_y, text, color, font);
    }

    void stop_animation()
    {
        if (!knob_) return;
        lv_anim_del(knob_, nullptr);
        knob_ = nullptr;
    }

    void key_event_cb(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            stop_animation();
            if (on_back) on_back();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            stop_animation();
            if (page_node_->Componens_api)
                page_node_->Componens_api(SettingApiActivate, this);
            if (confirm_label_)
                lv_label_set_text(confirm_label_, "Reboot requested     ESC: back");
        }
        lv_event_stop_processing(event);
    }

    NodeIter page_node_;
    std::function<void()> on_back;
    lv_obj_t *knob_ = nullptr;
    lv_obj_t *confirm_label_ = nullptr;
};
