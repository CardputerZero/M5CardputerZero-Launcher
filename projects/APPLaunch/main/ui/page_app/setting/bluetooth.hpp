#pragma once

#include "menu_types.hpp"
#include "../../model/async_operation_lifecycle.hpp"
#include "../../model/bluetooth_page_model.hpp"
#include "../../model/bluetooth_scan_lifecycle_contract.hpp"

#include "cp0_lvgl_app.h"
#include <lvgl.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

class UISetupPage;

namespace setting {

class Bluetooth
{
public:
    Bluetooth();
    ~Bluetooth();
    void shutdown();

    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void enter_devices(UISetupPage &page);
    void enter_alias(UISetupPage &page);
    void build_alias_view(UISetupPage &page);
    void handle_alias_key(UISetupPage &page, uint32_t key);
    void handle_power_warning_key(UISetupPage &page, uint32_t key);
    void enter_scan(UISetupPage &page);
    void build_list(UISetupPage &page);
    void handle_list_key(UISetupPage &page, uint32_t key);
    void refresh_status(UISetupPage &page);
    void toggle_power(UISetupPage &page);
    void toggle_named_only(UISetupPage &page);
    void toggle_discoverable(UISetupPage &page);
    void start_scan_timer(UISetupPage &page);
    void stop_scan_timer();
    void refresh_devices();
    void do_scan(UISetupPage &page);

private:
    bool require_power_enabled(UISetupPage &page);
    void alias_update_display();
    void rebuild_rows();
    void show_action(UISetupPage &page, const char *message, uint32_t color = 0x58A6FF);
    bool show_power_warning(UISetupPage &page);
    void activate_selected(UISetupPage &page);
    void remove_selected(UISetupPage &page);
    void start_failure_feedback(UISetupPage &page);
    void stop_failure_feedback();
    void suspend_scan_discovery();
    void resume_scan_discovery();
    static void scan_timer_cb(lv_timer_t *timer) noexcept;
    static void failure_feedback_timer_cb(lv_timer_t *timer) noexcept;
    static void failure_feedback_screen_delete_cb(lv_event_t *event) noexcept;
    static void copy_string(char *destination, size_t destination_size, const std::string &source);
    static bool decode_status(const std::string &data, cp0_bt_status_t &status);
    static int decode_devices(const std::string &data, cp0_bt_device_t *out, int max_devices);
    static int api_int(std::list<std::string> args, int default_value = -1);
    static cp0_bt_status_t get_status();
    static int set_power(int enabled);
    static int set_alias(const std::string &alias);
    static int set_discoverable(int enabled);
    static int device_command(const char *command, const char *address);
    static int device_list(const char *command, cp0_bt_device_t *out, int max_devices);
    static int rfkill_blocked();

    AsyncOperationLifecycle action_operation_;
    AsyncOperationLifecycle sudo_operation_;
    AsyncOperationLifecycle::Token action_token_;
    cp0_bt_device_t devices_[CP0_BT_DEVICE_MAX];
    int device_count_ = 0;
    BluetoothPageModel model_;
    lv_timer_t *scan_timer_ = nullptr;
    UISetupPage *scan_page_ = nullptr;
    bool scan_callback_enabled_ = false;
    lv_timer_t *failure_feedback_timer_ = nullptr;
    UISetupPage *failure_feedback_page_ = nullptr;
    bool discovery_active_ = false;
    bool action_busy_ = false;
    lv_obj_t *alias_input_lbl_ = nullptr;
    lv_obj_t *alias_hint_lbl_ = nullptr;
};

} // namespace setting
