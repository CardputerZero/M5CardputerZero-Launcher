#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <cstddef>
#include <cstdint>
#include <string>

#include "cp0_bounded_task_registry.hpp"
#include "cp0_lvgl_app.h"
#include "cp0_lvgl_app_page_assets.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "settings_fonts.hpp"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

class LvSettingWifiScanPage3 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        ScreenW          = 320,
        ScreenH          = 150,
        VisibleRows      = 5,
        RowH             = 22,
        RowY             = 30,
        TitleW           = 300,
        ApMax            = CP0_WIFI_AP_MAX,
        MaxSsidBytes     = 32,
        MaxPasswordBytes = 64,
        HiddenInputX     = 82,
        HiddenInputW     = 216,
        HiddenInputH     = 28,
        PasswordTextX          = 8,
        PasswordTextRightInset = 8,
        CursorGap              = 2,
        CursorWidth            = 2,
        CursorHeight           = 20,
        HiddenInputLetterSpace = 1,
        HiddenInputCursorWidth = 1,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingWifiScanPage3();

    LvSettingWifiScanPage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback);

    LvSettingWifiScanPage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback,
                           bool hidden_network);

    LvSettingWifiScanPage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback,
                           bool hidden_network, bool wifi_power_enabled);

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;

    ~LvSettingWifiScanPage3() override;

    void create_ui(lv_obj_t *parent) override;

private:
    enum class View { List, HiddenSsid, Password, Connecting };
    enum class NetworkOperation { Connect, Forget };
    enum class ConnectionOrigin {
        OpenNetwork,
        SavedProfile,
        PasswordEntry,
        HiddenPasswordEntry,
    };

    struct RowObjects {
        lv_obj_t *background = nullptr;
        lv_obj_t *ssid       = nullptr;
        lv_obj_t *security   = nullptr;
        lv_obj_t *signal     = nullptr;
        bool visible         = false;
        bool selected        = false;
        uint32_t color       = 0;
    };

    struct ScanState;
    struct ScanResult;
    struct ConnectionState;
    struct ConnectionResult;
    struct WifiData;

    struct UiDispatchState {
        std::mutex mutex;
        bool stopped = false;
        std::deque<std::function<void()>> pending;
    };

    static void mark_scan_dispatch_failed(const std::shared_ptr<ScanState> &state) noexcept;

    static void mark_connection_dispatch_failed(const std::shared_ptr<ConnectionState> &state) noexcept;

    static bool enqueue_ui_task(const std::shared_ptr<UiDispatchState> &dispatch, std::function<void()> task) noexcept;

    static void ui_dispatch_timer_cb(lv_timer_t *timer) noexcept;

    static bool enqueue_scan_result(const std::shared_ptr<UiDispatchState> &dispatch,
                                    const std::shared_ptr<ScanResult> &result) noexcept;

    static bool enqueue_scan_failure(const std::shared_ptr<UiDispatchState> &dispatch,
                                     const std::shared_ptr<ScanState> &state) noexcept;

    static bool enqueue_connection_result(const std::shared_ptr<UiDispatchState> &dispatch,
                                          const std::shared_ptr<ConnectionResult> &result) noexcept;

    static bool enqueue_connection_failure(const std::shared_ptr<UiDispatchState> &dispatch,
                                           const std::shared_ptr<ConnectionState> &state) noexcept;

    static lv_obj_t *create_label(lv_obj_t *parent, const char *text, int x, int y, uint32_t color,
                                  const lv_font_t *font);

    static const lv_font_t *input_font(uint16_t size);

    static const char *scan_error_message(int result);

    static const char *connection_error_message(int result);

    static bool is_open_security(const std::string &security);

    static bool is_utf8_continuation(unsigned char value);

    static std::size_t previous_utf8_start(const std::string &value, std::size_t cursor);

    static std::size_t next_utf8_end(const std::string &value, std::size_t cursor);

    static std::size_t utf8_cursor_position(const std::string &value, std::size_t cursor);

    static std::string masked_password(const std::string &password, bool visible);

    static std::size_t display_cursor_offset(const std::string &password, std::size_t cursor, bool visible);

    void initialize(lv_obj_t *parent);

    void create_password_panel();

    void render_password_editor();

    static void password_cursor_timer_cb(lv_timer_t *timer) noexcept;

    static lv_obj_t *create_hidden_input(lv_obj_t *parent, int y, uint32_t max_length);

    void create_hidden_network_panel();

    void render_password_panel();

    void update_hidden_input(lv_obj_t *input, const std::string &value, std::size_t cursor_position);

    void set_hidden_focus(int focus);

    void render_hidden_network_panel();

    void refresh_status();

    void show_power_warning();

    void close_power_warning();

    void render();

    static void set_row_text(lv_obj_t *label, const std::string &text);

    static void set_row_text(lv_obj_t *label, const char *text);

    static void hide_row(RowObjects &row);

    static void show_row(RowObjects &row);

    void enter_text_input_mode();

    void restore_text_input_mode();

    void clear_password();

    void clear_hidden_ssid();

    static bool append_text(std::string &value, std::size_t &cursor, const char *text, std::size_t max_bytes);

    bool append_password_text(const char *text);

    bool append_hidden_ssid_text(const char *text);

    bool erase_password_last();

    bool move_password_cursor_left();

    bool move_password_cursor_right();

    bool erase_hidden_ssid_last();

    bool move_hidden_ssid_cursor_left();

    bool move_hidden_ssid_cursor_right();

    bool append_hidden_text(const char *text);

    bool erase_hidden_text();

    bool move_hidden_cursor_left();

    bool move_hidden_cursor_right();

    std::string password_validation_error() const;

    void show_password_prompt(const std::string &ssid, const std::string &security, const std::string &error = {});

    void show_hidden_ssid_prompt(const std::string &initial = {});

    void leave_hidden_ssid_prompt();

    void submit_hidden_ssid();

    void leave_password_prompt();

    void cancel_password_prompt();

    void submit_password();

    bool start_network_operation(NetworkOperation operation, const std::string &ssid, const std::string &password,
                                 const std::string &security, ConnectionOrigin origin, bool disconnect_active = false);

    bool start_connection(const std::string &ssid, const std::string &password, ConnectionOrigin origin,
                          const std::string &security = {});

    void stop_connection();

    void cancel_connection();

    void process_connection_result(const ConnectionResult &result);

    void handle_dispatch_failures();

    void activate_selected();

    void forget_selected();

    static void keyboard_event_cb(lv_event_t *event);

    void start_scan();

    void stop_scan();

    void process_scan_result(const ScanResult &result);

    void apply_scan_result(const ScanResult &result);

    bool move_selection(int delta);

    void handle_key_event(lv_event_t *event);

    std::shared_ptr<bool> lifetime_token_         = std::make_shared<bool>(true);
    std::shared_ptr<UiDispatchState> ui_dispatch_ = std::make_shared<UiDispatchState>();
    Cp0BoundedTaskRegistry scan_tasks_;
    Cp0BoundedTaskRegistry connection_tasks_;
    std::shared_ptr<ScanState> scan_state_;
    std::shared_ptr<ConnectionState> connection_state_;
    NodeIter parent_node_;
    std::array<RowObjects, static_cast<std::size_t>(LayoutMetric::VisibleRows)> rows_{};
    std::unique_ptr<WifiData> wifi_data_;
    int selected_index_        = 0;
    bool scanning_             = false;
    bool scan_restart_pending_ = false;
    bool connection_pending_   = false;
    View view_                 = View::List;
    std::string selected_ssid_before_scan_;
    std::string title_text_;
    std::string scan_error_;
    std::string password_ssid_;
    std::string password_security_;
    std::string password_;
    std::string password_error_;
    std::string hidden_ssid_;
    std::size_t hidden_ssid_cursor_byte_                 = 0;
    std::size_t password_cursor_byte_                    = 0;
    int hidden_focus_                                    = 0;
    bool password_visible_                               = false;
    bool password_cursor_visible_                        = true;
    bool hidden_network_                                 = false;
    bool wifi_power_enabled_                             = true;
    uint64_t generation_                                 = 0;
    bool keyboard_mode_saved_                            = false;
    int previous_keypad_intercept_                       = 0;
    cp0_keyboard_input_context_t previous_input_context_ = KBD_INPUT_CONTEXT_NAVIGATION;
    lv_obj_t *title_                                     = nullptr;
    lv_obj_t *empty_                                     = nullptr;
    lv_obj_t *hint_                                      = nullptr;
    lv_obj_t *password_panel_                            = nullptr;
    lv_obj_t *password_title_                            = nullptr;
    lv_obj_t *password_network_                          = nullptr;
    lv_obj_t *password_value_                            = nullptr;
    lv_obj_t *password_prefix_                           = nullptr;
    lv_obj_t *password_suffix_                           = nullptr;
    lv_obj_t *password_cursor_bar_                       = nullptr;
    lv_obj_t *password_status_                           = nullptr;
    lv_obj_t *password_hint_                             = nullptr;
    lv_obj_t *hidden_panel_                              = nullptr;
    lv_obj_t *hidden_ssid_input_                         = nullptr;
    lv_obj_t *hidden_password_input_                     = nullptr;
    lv_obj_t *hidden_hint_                               = nullptr;
    lv_obj_t *keyboard_root_                             = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_                  = nullptr;
    lv_timer_t *password_cursor_timer_                   = nullptr;
    lv_timer_t *ui_dispatch_timer_                       = nullptr;
    lv_obj_t *power_warning_overlay_                     = nullptr;
    lv_obj_t *power_warning_                             = nullptr;
};
