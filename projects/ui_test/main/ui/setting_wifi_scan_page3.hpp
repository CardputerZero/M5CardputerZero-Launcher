#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cp0_bounded_task_registry.hpp"
#include "cp0_font_service.hpp"
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
    enum class LayoutMetric : int {
        ScreenW        = 320,
        ScreenH        = 150,
        VisibleRows    = 5,
        RowH           = 22,
        RowY           = 30,
        TitleW         = 300,
        MockApMax      = 32,
        MaxPasswordBytes = 64,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingWifiScanPage3() = default;

    LvSettingWifiScanPage3(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback)
        : parent_node_(parent_node)
    {
        LeaveSelfPage = std::move(back_callback);
        initialize(parent);
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
            std::bind(&LvSettingWifiScanPage3::handle_key_event,
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
            lv_obj_set_width(title_, metric(LayoutMetric::TitleW));
            lv_label_set_long_mode(title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        }

        create_label(ComponensObj, "SSID", 8, 18, 0x888888, &lv_font_montserrat_10);
        create_label(ComponensObj, "Security", 180, 18, 0x888888, &lv_font_montserrat_10);
        create_label(ComponensObj, "Signal", 270, 18, 0x888888, &lv_font_montserrat_10);

        for (int index = 0; index < metric(LayoutMetric::VisibleRows); ++index) {
            auto &row = rows_[index];
            const int y = metric(LayoutMetric::RowY) + index * metric(LayoutMetric::RowH);

            row.background = lv_obj_create(ComponensObj);
            if (row.background) {
                lv_obj_set_size(row.background,
                                metric(LayoutMetric::ScreenW) - 8,
                                metric(LayoutMetric::RowH) - 2);
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
            ComponensObj,
            "",
            8,
            metric(LayoutMetric::ScreenH) - 14,
            0x555555,
            &lv_font_montserrat_10);
        create_password_panel();

        initialize_mock_data();
        refresh_status();
        render();
        start_scan();
    }

private:
    struct MockAccessPoint {
        std::string ssid;
        std::string security;
        int signal = 0;
        bool in_use = false;
        bool saved = false;
    };

    enum class MockError : int {
        RadioOff = -2,
        Auth     = -3,
        NotFound = -4,
        IpConfig = -5,
        Service  = -6,
        Timeout  = -7,
    };

    static constexpr int error_code(MockError value)
    {
        return static_cast<int>(value);
    }

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
        std::array<MockAccessPoint,
                   static_cast<std::size_t>(LayoutMetric::MockApMax)> access_points{};
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
        std::string security;
    };

    struct ConnectionResult {
        std::shared_ptr<ConnectionState> state;
        int result = error_code(MockError::Service);
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
        case error_code(MockError::RadioOff): return "WiFi is off. Turn on Power to scan";
        case error_code(MockError::Timeout): return "WiFi scan timed out. Press R to retry";
        case error_code(MockError::Service): return "WiFi service unavailable. Press R to retry";
        default: return "WiFi scan failed. Press R to retry";
        }
    }

    static const char *connection_error_message(int result)
    {
        switch (result) {
        case error_code(MockError::RadioOff): return "WiFi is off";
        case error_code(MockError::Auth): return "Incorrect password";
        case error_code(MockError::NotFound): return "Network is no longer available";
        case error_code(MockError::IpConfig): return "Connected, but IP setup failed";
        case error_code(MockError::Timeout): return "Network operation timed out";
        default: return "WiFi service unavailable";
        }
    }

    static bool is_open_security(const std::string &security)
    {
        if (security.empty()) return true;
        std::string value = security;
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
        lv_obj_set_size(password_panel_,
                        metric(LayoutMetric::ScreenW),
                        metric(LayoutMetric::ScreenH));
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
            password_panel_,
            "",
            8,
            metric(LayoutMetric::ScreenH) - 14,
            0x555555,
            &lv_font_montserrat_10);

        if (password_network_) lv_obj_set_width(password_network_, metric(LayoutMetric::ScreenW) - 16);
        if (password_value_) {
            lv_obj_set_width(password_value_, metric(LayoutMetric::ScreenW) - 16);
            lv_label_set_long_mode(password_value_, LV_LABEL_LONG_CLIP);
        }
        if (password_status_) lv_obj_set_width(password_status_, metric(LayoutMetric::ScreenW) - 16);
        if (password_hint_) lv_obj_set_width(password_hint_, metric(LayoutMetric::ScreenW) - 16);
        lv_obj_add_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);
    }

    static MockAccessPoint make_mock_access_point(const char *ssid,
                                                  const char *security,
                                                  int signal,
                                                  bool saved)
    {
        MockAccessPoint access_point;
        access_point.ssid = ssid ? ssid : "";
        access_point.security = security ? security : "OPEN";
        access_point.signal = signal;
        access_point.saved = saved;
        return access_point;
    }

    void initialize_mock_data()
    {
        mock_networks_.clear();
        mock_networks_.push_back(make_mock_access_point("CardputerZero", "WPA2", 96, true));
        mock_networks_.push_back(make_mock_access_point("Office WiFi", "WPA2", 84, true));
        mock_networks_.push_back(make_mock_access_point("M5Stack Guest", "OPEN", 72, false));
        mock_networks_.push_back(make_mock_access_point("IoT-Lab", "WPA3", 61, false));
        mock_networks_.push_back(make_mock_access_point("Cafe Free", "OPEN", 48, false));
        mock_networks_.push_back(make_mock_access_point("Workshop", "WPA2", 35, false));
        mock_connected_ssid_ = "CardputerZero";
        mock_ip_ = "192.168.1.42";
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
        if (!mock_connected_ssid_.empty()) {
            title_text_ = "Connected WiFi: ";
            title_text_ += mock_connected_ssid_;
            title_text_ += "  ";
            title_text_ += mock_ip_;
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
        int offset = selected_index_ - metric(LayoutMetric::VisibleRows) / 2;
        offset = std::max(0,
                          std::min(offset,
                                   std::max(0, count - metric(LayoutMetric::VisibleRows))));

        for (int visible = 0; visible < metric(LayoutMetric::VisibleRows); ++visible) {
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
                         access_point.security.empty() ? "Open" : access_point.security);
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
        if (value.empty() ||
            password_.size() + value.size() >
                static_cast<std::size_t>(metric(LayoutMetric::MaxPasswordBytes)))
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
                                 ConnectionOrigin origin)
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
                int result = 0;
                for (int index = 0; index < 5; ++index) {
                    if (state->stop.load(std::memory_order_acquire)) return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(60));
                }
                if (state->operation == NetworkOperation::Connect &&
                    state->origin == ConnectionOrigin::PasswordEntry &&
                    state->password == "wrong")
                    result = error_code(MockError::Auth);
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
            if (result->result == 0) {
                self->mock_networks_.erase(
                    std::remove_if(self->mock_networks_.begin(),
                                   self->mock_networks_.end(),
                                   [&state](const MockAccessPoint &access_point) {
                                       return state->ssid == access_point.ssid;
                                   }),
                    self->mock_networks_.end());
                if (self->mock_connected_ssid_ == state->ssid) {
                    self->mock_connected_ssid_.clear();
                    self->mock_ip_.clear();
                }
            }
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
            self->mock_connected_ssid_ = state->ssid;
            self->mock_ip_ = "192.168.1.99";
            for (auto &access_point : self->mock_networks_)
                access_point.in_use = state->ssid == access_point.ssid ? 1 : 0;
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

        const MockAccessPoint access_point =
            access_points_[static_cast<std::size_t>(selected_index_)];
        if (access_point.in_use || access_point.ssid.empty()) return;
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
        const MockAccessPoint &access_point =
            access_points_[static_cast<std::size_t>(selected_index_)];
        if (!access_point.saved || access_point.ssid.empty()) return;
        stop_scan();
        start_network_operation(
            NetworkOperation::Forget,
            access_point.ssid,
            {},
            access_point.security,
            ConnectionOrigin::OpenNetwork);
    }

    static void keyboard_event_cb(lv_event_t *event)
    {
        if (!event) return;
        auto *self = static_cast<LvSettingWifiScanPage3 *>(lv_event_get_user_data(event));
        auto *item = static_cast<const key_item *>(lv_event_get_param(event));
        if (!self || !item) return;

        const bool pressed = item->key_state == KBD_KEY_PRESSED ||
                             item->key_state == KBD_KEY_REPEATED;
        if (!pressed) return;

        if (self->view_ == View::Password) {
            if (item->key_code == KEY_ESC) {
                self->leave_password_prompt();
                self->start_scan();
            } else if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
                self->submit_password();
            } else if (item->key_code == KEY_BACKSPACE || item->key_code == KEY_DELETE) {
                self->erase_password_last();
                self->render();
            } else if (item->key_code == KEY_LEFT) {
                self->move_password_cursor_left();
                self->render();
            } else if (item->key_code == KEY_RIGHT) {
                self->move_password_cursor_right();
                self->render();
            } else if (item->key_code == KEY_LEFTALT) {
                self->password_visible_ = !self->password_visible_;
                self->render();
            } else if (item->utf8[0]) {
                self->append_password_text(item->utf8);
                self->render();
            }
            lv_event_stop_processing(event);
            return;
        }

        if (item->key_code == KEY_R || item->semantic_key == KEY_R) {
            self->start_scan();
        } else if (item->key_code == KEY_D || item->semantic_key == KEY_D) {
            self->forget_selected();
        }
        lv_event_stop_processing(event);
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

        const std::vector<MockAccessPoint> mock_networks = mock_networks_;
        const std::string mock_connected_ssid = mock_connected_ssid_;

        auto state = std::make_shared<ScanState>();
        state->lifetime = lifetime_token_;
        state->owner = this;
        scan_state_ = state;

        if (!scan_tasks_.start([state, mock_networks, mock_connected_ssid] {
                auto result = std::make_unique<ScanResult>();
                result->state = state;
                for (int index = 0; index < 5; ++index) {
                    if (state->stop.load(std::memory_order_acquire)) return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                result->count = 0;
                for (const auto &source : mock_networks) {
                    if (result->count >= metric(LayoutMetric::MockApMax)) break;
                    MockAccessPoint access_point = source;
                    access_point.in_use = mock_connected_ssid == access_point.ssid;
                    result->access_points[static_cast<std::size_t>(result->count++)] =
                        access_point;
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
        const int count = std::clamp(result.count, 0, metric(LayoutMetric::MockApMax));
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
                    [this](const MockAccessPoint &access_point) {
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
        if (view_ != View::List || access_points_.empty() || delta == 0) return false;
        const int next = std::clamp(
            selected_index_ + delta,
            0,
            static_cast<int>(access_points_.size()) - 1);
        if (next == selected_index_) return false;
        selected_index_ = next;
        return true;
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        const uint32_t key = lv_event_get_key(event);

        if (view_ == View::Password) {
            if (key == LV_KEY_ESC) {
                leave_password_prompt();
                start_scan();
            } else if (key == LV_KEY_ENTER) {
                submit_password();
            } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
                erase_password_last();
                render();
            } else if (key == LV_KEY_LEFT) {
                move_password_cursor_left();
                render();
            } else if (key == LV_KEY_RIGHT) {
                move_password_cursor_right();
                render();
            }
            lv_event_stop_processing(event);
            return;
        }

        if (view_ == View::Connecting) {
            if (key == LV_KEY_ESC) {
                stop_connection();
                view_ = View::List;
                scan_error_.clear();
                render();
                start_scan();
            } else if (key == LV_KEY_LEFT && LeaveSelfPage) {
                stop_connection();
                LeaveSelfPage();
            }
            lv_event_stop_processing(event);
            return;
        }

        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            stop_scan();
            if (LeaveSelfPage) LeaveSelfPage();
        } else if (key == LV_KEY_UP) {
            if (move_selection(-1)) render();
        } else if (key == LV_KEY_DOWN) {
            if (move_selection(1)) render();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            activate_selected();
        } else if (key == LV_KEY_DEL) {
            forget_selected();
        } else if (key == LV_KEY_NEXT) {
            start_scan();
        }
        lv_event_stop_processing(event);
    }

    std::shared_ptr<bool> lifetime_token_ = std::make_shared<bool>(true);
    Cp0BoundedTaskRegistry scan_tasks_;
    Cp0BoundedTaskRegistry connection_tasks_;
    std::shared_ptr<ScanState> scan_state_;
    std::shared_ptr<ConnectionState> connection_state_;
    NodeIter parent_node_;
    std::array<RowObjects,
               static_cast<std::size_t>(LayoutMetric::VisibleRows)> rows_{};
    std::vector<MockAccessPoint> access_points_;
    std::vector<MockAccessPoint> mock_networks_;
    int selected_index_ = 0;
    bool scanning_ = false;
    bool scan_restart_pending_ = false;
    bool connection_pending_ = false;
    View view_ = View::List;
    std::string selected_ssid_before_scan_;
    std::string title_text_;
    std::string scan_error_;
    std::string mock_connected_ssid_;
    std::string mock_ip_;
    std::string password_ssid_;
    std::string password_security_;
    std::string password_;
    std::string password_error_;
    std::size_t password_cursor_byte_ = 0;
    bool password_visible_ = false;
    bool keyboard_mode_saved_ = false;
    int previous_keypad_intercept_ = 0;
    cp0_keyboard_input_context_t previous_input_context_ = KBD_INPUT_CONTEXT_NAVIGATION;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *empty_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    lv_obj_t *password_panel_ = nullptr;
    lv_obj_t *password_title_ = nullptr;
    lv_obj_t *password_network_ = nullptr;
    lv_obj_t *password_value_ = nullptr;
    lv_obj_t *password_status_ = nullptr;
    lv_obj_t *password_hint_ = nullptr;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
};
