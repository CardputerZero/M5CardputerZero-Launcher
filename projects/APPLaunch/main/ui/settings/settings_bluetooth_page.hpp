#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cp0_bounded_task_registry.hpp"
#include "../../../../ext_components/cp0_lvgl/src/cp0_signal_registration.hpp"
#include "settings_fonts.hpp"
#include "cp0_lvgl_app.h"
#include "cp0_lvgl_app_page_assets.h"
#include "hal_lvgl_bsp.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

enum class LvSettingBluetoothListMode { Connected, Scan };

class LvSettingBluetoothPage3 : public DComponens::LvglComponensBase {
public:
    struct AgentPromptRequest {
        uint64_t id = 0;
        std::string method;
        std::string device;
        // RequestConfirmation carries the numeric comparison code in the
        // fourth signal argument (the same field is used for other hints).
        std::string hint;
        std::function<void(bool, std::string)> reply;
    };
    enum class LayoutMetric : int {
        ScreenW     = 320,
        ScreenH     = 150,
        RowH        = 22,
        RowY        = 22,
        ConnectedRowY = 22,
        ScanSectionY = 23,
        ScanRowY    = 38,
        VisibleRows = 4,
        HintY       = ScreenH - 14,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }
    LvSettingBluetoothPage3() = default;

    LvSettingBluetoothPage3(lv_obj_t *parent,
                            const NodeIter &parent_node,
                            std::function<void()> back_callback,
                            LvSettingBluetoothListMode mode);

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;

    ~LvSettingBluetoothPage3() override;

    void create_ui(lv_obj_t *parent) override;

private:
    using ApiHandler = std::function<void(int, std::string)>;

    struct ApiResult {
        LvSettingBluetoothPage3 *owner = nullptr;
        std::weak_ptr<bool> lifetime;
        ApiHandler handler;
        ApiHandler stale_handler;
        uint64_t generation = 0;
        int code = -1;
        std::string data;
    };

    struct ApiDispatchState {
        std::mutex mutex;
        bool stopped = false;
        std::deque<ApiResult> pending;
        std::deque<AgentPromptRequest> agent_events;
    };

    static void enqueue_api_result(const std::shared_ptr<ApiDispatchState> &dispatch,
                                   LvSettingBluetoothPage3 *owner,
                                   const std::weak_ptr<bool> &lifetime,
                                   const ApiHandler &handler,
                                   const ApiHandler &stale_handler,
                                   uint64_t generation,
                                   int code,
                                   std::string data) noexcept;

    static void api_result_timer_cb(lv_timer_t *timer) noexcept;

    void request_api(std::list<std::string> arguments,
                     ApiHandler handler,
                     ApiHandler stale_handler = {});

    static bool decode_status(const std::string &data,
                              bool &powered,
                              std::string &address);

    static std::string device_text(const char *value, size_t size);

    static lv_obj_t *create_label(lv_obj_t *parent,
                                  const char *text,
                                  int x,
                                  int y,
                                  int width,
                                  uint32_t color,
                                  const lv_font_t *font);

    void request_status();

    static void status_retry_timer_cb(lv_timer_t *timer) noexcept;

    void refresh_devices();

    void start_scan();

    void restart_scan();

    void stop_scan(std::function<void()> after_stop = {});

    static void scan_timer_cb(lv_timer_t *timer) noexcept;

    void submit_action(bool connected, bool paired);

    void activate_selected();

    void request_action(const char *command, const std::string &address);

    void show_agent_prompt(const AgentPromptRequest &request);
    void render_agent_prompt();
    void handle_agent_key(const key_item &item);
    void submit_agent_reply(bool accepted);
    void clear_agent_prompt();
    void reject_agent_prompt();

    void enter_text_input_mode();
    void restore_text_input_mode();

    void cancel_pairing_on_exit(const std::string &address);

    void cleanup_failed_pair(int code, std::string data);

    void cancel_action();

    void finish_action(int code, std::string data);

    void remove_selected();

    void clamp_selection();

    bool move_selection(int direction);

    void render();

    void show_power_warning();

    static void keyboard_event_cb(lv_event_t *event);

    void handle_key_event(lv_event_t *event);

    NodeIter parent_node_;
    LvSettingBluetoothListMode mode_ = LvSettingBluetoothListMode::Connected;
    std::vector<cp0_bt_device_t> devices_;
    int selected_index_ = 0;
    bool status_known_ = false;
    bool powered_ = false;
    bool status_pending_ = false;
    bool list_pending_ = false;
    bool loading_ = false;
    bool action_pending_ = false;
    bool pair_in_flight_ = false;
    bool leaving_ = false;
    bool action_waiting_for_scan_stop_ = false;
    bool pair_cleanup_pending_ = false;
    bool scan_start_pending_ = false;
    bool scan_stop_after_start_ = false;
    bool scan_stop_pending_ = false;
    bool discovery_active_ = false;
    bool agent_prompt_active_ = false;
    AgentPromptRequest agent_request_{};
    std::string agent_input_;
    std::string agent_error_;
    lv_obj_t *agent_overlay_ = nullptr;
    lv_obj_t *agent_input_textarea_ = nullptr;
    bool keyboard_mode_saved_ = false;
    int previous_keypad_intercept_ = 0;
    cp0_keyboard_input_context_t previous_input_context_ = KBD_INPUT_CONTEXT_NAVIGATION;
    std::string adapter_address_;
    std::string error_message_;
    std::string action_message_;
    std::string action_address_;
    std::function<void()> scan_after_stop_;
    uint64_t scan_stop_request_id_ = 0;
    lv_timer_t *scan_timer_ = nullptr;
    lv_timer_t *status_retry_timer_ = nullptr;
    lv_timer_t *api_timer_ = nullptr;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
    cp0::SignalRegistration<decltype(cp0_signal_bt_agent)> agent_registration_;
    bool warning_active_ = false;
    // KEY_R (Linux value 19) is numerically identical to LV_KEY_RIGHT. The
    // raw keyboard callback handles restart, then consumes the matching LVGL
    // navigation event generated for that same physical key.
    bool suppress_next_navigation_key_ = false;
    Cp0BoundedTaskRegistry api_tasks_;
    std::shared_ptr<ApiDispatchState> api_dispatch_ = std::make_shared<ApiDispatchState>();
    std::shared_ptr<bool> page_lifetime_ = std::make_shared<bool>(true);
    uint64_t generation_ = 1;
};
class LvSettingBluetoothAliasPage3 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        ScreenW = 320,
        ScreenH = 150,
        MaxAliasBytes = CP0_BT_NAME_MAX - 1,
        AliasTextX = 64,
        AliasTextRightInset = 8,
        AliasInputLetterSpace = 1,
        AliasInputCursorWidth = 1,
        CursorGap = 2,
        CursorWidth = 2,
        CursorHeight = 18,
        HintY = ScreenH - 14,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingBluetoothAliasPage3();

    LvSettingBluetoothAliasPage3(lv_obj_t *parent,
                                 const NodeIter &parent_node,
                                 std::function<void()> back_callback,
                                 std::string initial_alias,
                                 std::function<void(std::string)> saved_callback);

    ~LvSettingBluetoothAliasPage3() override;

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;

    void create_ui(lv_obj_t *parent) override;

private:
    using ApiHandler = std::function<void(int, std::string)>;

    struct ApiDispatchState;

    static void enqueue_api_result(const std::shared_ptr<ApiDispatchState> &dispatch,
                                   const std::weak_ptr<bool> &lifetime,
                                   ApiHandler handler,
                                   ApiHandler stale_handler,
                                   uint64_t generation,
                                   int code,
                                   std::string data) noexcept;

    static lv_obj_t *create_label(lv_obj_t *parent,
                                  const char *text,
                                  int x,
                                  int y,
                                  int width,
                                  uint32_t color,
                                  const lv_font_t *font);

    static const lv_font_t *input_font(uint16_t size);

    static std::size_t previous_utf8_start(const std::string &value, std::size_t cursor);

    static std::size_t next_utf8_end(const std::string &value, std::size_t cursor);

    static std::size_t byte_cursor_from_char_pos(const std::string &value,
                                                 std::size_t char_pos);

    void render();

    void show_power_warning();

    void request_api(std::list<std::string> arguments,
                     ApiHandler handler,
                     ApiHandler stale_handler = {});

    void request_status();

    static void status_retry_timer_cb(lv_timer_t *timer) noexcept;

    static void api_result_timer_cb(lv_timer_t *timer) noexcept;

    static void cursor_timer_cb(lv_timer_t *timer) noexcept;

    void append_text(const char *text);

    void sync_textarea_state();

    void enter_text_input_mode();

    void restore_text_input_mode();

    void save();

    void handle_key_event(lv_event_t *event);

    static void keyboard_event_cb(lv_event_t *event);

    NodeIter parent_node_;
    std::string alias_;
    std::string backend_alias_;
    std::string alias_before_save_;
    std::size_t cursor_ = 0;
    bool saving_ = false;
    bool alias_edited_ = false;
    bool cursor_visible_ = true;
    bool status_known_ = false;
    bool powered_ = false;
    bool status_pending_ = false;
    bool warning_active_ = false;
    std::string error_message_;
    lv_obj_t *alias_input_ = nullptr;
    std::function<void(std::string)> saved_callback_;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
    lv_timer_t *api_timer_ = nullptr;
    lv_timer_t *cursor_timer_ = nullptr;
    lv_timer_t *status_retry_timer_ = nullptr;
    bool keyboard_mode_saved_ = false;
    int previous_keypad_intercept_ = 0;
    cp0_keyboard_input_context_t previous_input_context_ = KBD_INPUT_CONTEXT_NAVIGATION;
    Cp0BoundedTaskRegistry api_tasks_;
    std::shared_ptr<ApiDispatchState> api_dispatch_;
    std::shared_ptr<bool> lifetime_ = std::make_shared<bool>(true);
    uint64_t generation_ = 1;
};
