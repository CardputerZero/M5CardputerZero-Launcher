#pragma once

#include "menu_types.hpp"
#include "../../model/async_operation_lifecycle.hpp"
#include "../../model/setup_wifi_model.hpp"

#include "cp0_lvgl_app.h"
#include "cp0_bounded_task_registry.hpp"
#include <lvgl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class UISetupPage;

namespace setting {

class WiFiListView
{
public:
    ~WiFiListView();
    bool mount(UISetupPage &page);
    bool render(UISetupPage &page, const SetupWifiListSnapshot &snapshot);
    void unmount();
    bool mounted() const { return root_ != nullptr; }

private:
    struct RowObjects {
        lv_obj_t *background = nullptr;
        lv_obj_t *ssid = nullptr;
        lv_obj_t *security = nullptr;
        lv_obj_t *signal = nullptr;
        std::string ssid_text;
        std::string security_text;
        std::string signal_text;
        uint32_t color = 0;
        bool selected = false;
        bool visible = false;
    };

    static void root_delete_cb(lv_event_t *event) noexcept;
    void reset_objects();

    lv_obj_t *root_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *empty_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    std::string title_text_;
    std::string empty_text_;
    std::string hint_text_;
    RowObjects rows_[SetupWifiListViewModel::VISIBLE_ROWS]{};
};

class WiFiPasswordView
{
public:
    ~WiFiPasswordView();
    bool show(UISetupPage &page, const std::string &ssid);
    void update_password(const std::string &password);
    void toggle_password_visibility();
    void set_hint(const char *text, uint32_t color = 0x555555);
    void unmount();

private:
    static void root_delete_cb(lv_event_t *event) noexcept;
    void reset_objects();

    lv_obj_t *root_ = nullptr;
    lv_obj_t *input_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    bool password_visible_ = false;
};

class WiFiSsidView
{
public:
    ~WiFiSsidView();
    bool show(UISetupPage &page);
    void update_ssid(const std::string &ssid);
    void update_password(const std::string &password);
    void set_focus(int focus);
    void toggle_password_visibility();
    void set_hint(const char *text, uint32_t color = 0x555555);
    void unmount();

private:
    static void root_delete_cb(lv_event_t *event) noexcept;
    void reset_objects();
    lv_obj_t *root_ = nullptr;
    lv_obj_t *ssid_input_ = nullptr;
    lv_obj_t *password_input_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    bool password_visible_ = false;
};

class WiFi
{
public:
    ~WiFi();
    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void enter_scan(UISetupPage &page);
    void enter_hidden_wifi(UISetupPage &page);
    bool build_list(UISetupPage &page);
    void handle_list_key(UISetupPage &page, uint32_t key);
    void refresh_radio(UISetupPage &page);
    void toggle_enable(UISetupPage &page);
    void try_connect(UISetupPage &page, int index);
    bool show_connecting(UISetupPage &page, const char *ssid);
    bool show_error(UISetupPage &page, const char *message);
    bool show_power_warning(UISetupPage &page);
    bool show_forget_confirmation(UISetupPage &page, const std::string &ssid);
    void forget_selected(UISetupPage &page);
    void handle_forget_key(UISetupPage &page, uint32_t key);
    void handle_power_warning_key(UISetupPage &page, uint32_t key);
    void handle_pw_key(UISetupPage &page, uint32_t key);
    void handle_ssid_key(UISetupPage &page, uint32_t key);
    void shutdown();

private:
    enum class ConnectionOrigin
    {
        OPEN_NETWORK,
        SAVED_PROFILE,
        PASSWORD_ENTRY,
        HIDDEN_PASSWORD_ENTRY,
    };

    struct ScanState;
    struct ScanResult;
    struct ConnectionResult;
    bool require_radio_enabled(UISetupPage &page);
    void start_scan(UISetupPage &page);
    void stop_scan();
    void request_scan();
    void refresh_list_status();
    void apply_scan_result(UISetupPage &page, const cp0_wifi_ap_t *aps, int count);
    static void scan_result_cb(void *user) noexcept;
    bool start_connection(UISetupPage &page, std::string ssid,
                          std::string password, ConnectionOrigin origin);
    static void connection_result_cb(void *user) noexcept;
    void clear_password_view();
    void clear_ssid_view();
    void start_connection_failure_feedback(UISetupPage &page);
    void stop_connection_failure_feedback();
    static void connection_feedback_timer_cb(lv_timer_t *timer) noexcept;
    static void feedback_screen_delete_cb(lv_event_t *event) noexcept;

    SetupWifiListViewModel list_view_model_;
    WiFiListView list_view_;
    WiFiPasswordView password_view_;
    WiFiSsidView ssid_view_;
    SetupWifiPasswordModel password_model_;
    SetupWifiSsidModel ssid_model_;
    int hidden_focus_ = 0;
    SetupWifiFeedbackModel feedback_model_;
    SetupWifiFeedbackModel::Token feedback_token_ = 0;
    lv_timer_t *feedback_timer_ = nullptr;
    UISetupPage *feedback_page_ = nullptr;
    std::string forget_ssid_;
    bool forget_active_ = false;
    std::shared_ptr<ScanState> scan_state_;
    Cp0BoundedTaskRegistry scan_tasks_;
    Cp0BoundedTaskRegistry connection_tasks_;
    AsyncOperationLifecycle connection_operation_;
};

} // namespace setting
