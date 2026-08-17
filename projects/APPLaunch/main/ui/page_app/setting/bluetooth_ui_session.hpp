#pragma once

#include "menu_types.hpp"
#include "../../model/async_operation_lifecycle.hpp"
#include "../../model/async_timeout_guard.hpp"
#include "../../model/bluetooth_ui_model.hpp"

#include "cp0_lvgl_app.h"
#include <lvgl.h>

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

class UISetupPage;

namespace setting {

// UI-side session object for the Bluetooth settings page. It is the only UI
// object that holds a UISetupPage*; backend worker threads never see that
// pointer — they deliver data through the cp0 session protocol, and the UI
// marshals the result to the LVGL main thread guarded by a weak_ptr so a
// queued completion is dropped after the session is gone.
//
// Owned as std::shared_ptr so backend deliveries can hold std::weak_ptr.
class BluetoothUiSession : public std::enable_shared_from_this<BluetoothUiSession>
{
public:
    explicit BluetoothUiSession(UISetupPage &page);
    ~BluetoothUiSession();

    BluetoothUiSession(const BluetoothUiSession &) = delete;
    BluetoothUiSession &operator=(const BluetoothUiSession &) = delete;

    // Builds the Bluetooth menu structure. Static: the session may not exist
    // yet when the main menu is assembled, so each action creates/accesses it
    // lazily through SetupPageAccess::ensure_bluetooth_ui().
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);

    // Called by UISetupPage::ensure_bluetooth_ui() after the shared_ptr is
    // fully constructed. Starts the initial status request path; the actual
    // BtStatusGet is deferred until on_sub_view_created() so the UI is built
    // (hidden) before the getter is issued.
    void begin_initial_load(UISetupPage &page);

    // Called by UISetupPage right after the Bluetooth SUB view has been
    // built. Hides the view until the initial status arrives, or shows the
    // failure dialog immediately when the 3-second load already failed.
    void on_sub_view_created(UISetupPage &page);

    void enter_devices(UISetupPage &page);
    void enter_scan(UISetupPage &page);
    void enter_alias(UISetupPage &page);

    void handle_list_key(UISetupPage &page, uint32_t key);
    void handle_alias_key(UISetupPage &page, uint32_t key);
    void handle_power_warning_key(UISetupPage &page, uint32_t key);
    void handle_timeout_key(UISetupPage &page, uint32_t key);

    void build_list(UISetupPage &page);
    void build_alias_view(UISetupPage &page);
    void build_timeout_view(UISetupPage &page);

    void refresh_status(UISetupPage &page);
    void request_initial_status(UISetupPage &page);
    void finish_session_load_failed(UISetupPage &page);
    void hide_content(UISetupPage &page);
    void reveal_content(UISetupPage &page);
    void toggle_power(UISetupPage &page);
    void toggle_named_only(UISetupPage &page);
    void toggle_discoverable(UISetupPage &page);

    bool session_ready() const { return session_ready_; }

private:
    struct UiPost {
        std::weak_ptr<BluetoothUiSession> weak;
        std::function<void(std::shared_ptr<BluetoothUiSession>)> fn;
    };

    static void post_to_ui(std::weak_ptr<BluetoothUiSession> weak,
                           std::function<void(std::shared_ptr<BluetoothUiSession>)> fn);
    static void ui_post_dispatch(void *user);

    // Issues a session-scoped cp0 Bluetooth call. The result callback runs on
    // the LVGL main thread; it is dropped entirely if the session is gone.
    void bt_api(std::list<std::string> args,
                std::function<void(std::shared_ptr<BluetoothUiSession>, int, std::string)> on_result);

    void set_power_async(int on);

    void apply_status_result(int code, BluetoothStatusSnapshot snapshot);
    void apply_connected_list(int code, BluetoothListSnapshot snapshot);
    void apply_scan_snapshot(int code, std::string data, uint64_t generation);
    void apply_power_result(int code, uint64_t generation);
    void apply_alias_result(std::string alias, int code);

    void enter_connected(UISetupPage &page);
    void leave_connected();
    void enter_scan_sub(UISetupPage &page);
    void leave_scan();
    void refresh_connected();

    void require_power_enabled(UISetupPage &page);
    void finish_device_action(const char *operation, int code);
    void alias_update_display();
    void activate_selected(UISetupPage &page);
    void remove_selected(UISetupPage &page);
    void show_action(UISetupPage &page, const char *message, uint32_t color = 0x58A6FF);
    bool show_power_warning(UISetupPage &page);

    void update_menu_from_status(UISetupPage &page);
    void start_timeout(UISetupPage &page, const char *message);
    void clear_timeout();
    void show_timeout_dialog(UISetupPage &page, const std::string &message);

    static int rfkill_blocked();
    static void copy_string(char *destination, size_t destination_size, const std::string &source);
    static void timeout_timer_cb(lv_timer_t *timer) noexcept;

    UISetupPage *page_ = nullptr;
    std::string session_id_;
    bool session_ready_ = false;

    BluetoothUiModel model_;

    bool connected_active_ = false;
    bool scan_active_ = false;
    bool action_busy_ = false;
    bool sub_view_hidden_ = false;
    uint64_t scan_generation_ = 0;

    AsyncTimeoutGuard timeout_;
    lv_timer_t *timeout_timer_ = nullptr;
    std::function<void()> timeout_pending_cb_;
    std::string timeout_message_;
    uint64_t power_generation_ = 0;

    AsyncOperationLifecycle action_operation_;
    AsyncOperationLifecycle::Token action_token_;
    AsyncOperationLifecycle sudo_operation_;

    lv_obj_t *alias_input_lbl_ = nullptr;
    lv_obj_t *alias_hint_lbl_ = nullptr;
};

} // namespace setting
