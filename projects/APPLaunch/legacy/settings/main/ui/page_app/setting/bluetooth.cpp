#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

#include <memory>
#include <mutex>
#include <new>
#include <thread>

namespace setting {

struct BluetoothUiSession::ActionState {
    std::mutex mutex;
    bool ready = false;
    int result = -1;
    bool from_scan = false;
};

static_assert(BluetoothPageModel::ALIAS_INPUT_LIMIT == CP0_BT_NAME_MAX - 1,
              "Bluetooth alias model must match the cp0 API buffer");

BluetoothUiSession::BluetoothUiSession() = default;

BluetoothUiSession::~BluetoothUiSession()
{
    shutdown();
}

void BluetoothUiSession::shutdown()
{
    stop_scan_timer();
    stop_failure_feedback();
    action_operation_.shutdown();
    if (action_result_timer_) {
        lv_timer_delete(action_result_timer_);
        action_result_timer_ = nullptr;
    }
    if (power_refresh_timer_) {
        lv_timer_delete(power_refresh_timer_);
        power_refresh_timer_ = nullptr;
    }
    power_refresh_page_ = nullptr;
    action_tasks_.join_all();
    action_state_.reset();
    action_page_ = nullptr;
    const uint64_t request_id = sudo_operation_.shutdown();
    if (request_id)
        cp0_signal_sudo_cancel(request_id, nullptr);
}

lv_result_t BluetoothUiSession::queue_lvgl_async(lv_async_cb_t callback, void *user_data)
{
    lv_lock();
    const lv_result_t result = lv_async_call(callback, user_data);
    lv_unlock();
    return result;
}

void BluetoothUiSession::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    UISetupPage *page = &p;
    SetupPageAccess access(*page);
    BluetoothUiSession *bt = &access.bluetooth();
    MenuItem m;
    m.label = "Bluetooth";
    bt->model_.set_named_only(access.config_get_int("bt_named_only", 1) != 0);
    m.sub_items = {
        {"Power", true, false, [bt, page]() { bt->toggle_power(*page); }},
        {"Alias: CardputerZero", false, false, [bt, page]() { bt->enter_alias(*page); }},
        {"Discoverable", true, false, [bt, page]() { bt->toggle_discoverable(*page); }},
        {"Named Only", true, bt->model_.named_only(), [bt, page]() { bt->toggle_named_only(*page); }},
        {"Connected", false, false, [bt, page]() { bt->enter_devices(*page); }},
        {"Scan", false, false, [bt, page]() { bt->enter_scan(*page); }},
    };
    m.on_enter = [bt, page]() { bt->refresh_status(*page); };
    menu.push_back(m);
}
void BluetoothUiSession::enter_devices(UISetupPage &page)
{
    if (!require_power_enabled(page)) return;
    stop_scan_timer();
    stop_failure_feedback();
    action_operation_.abort(action_token_);
    action_busy_ = false;
    model_.set_list_mode(BluetoothListMode::MANAGED);
    SetupPageAccess(page).set_view(SetupViewState::BT_LIST);
    refresh_devices();
    build_list(page);
}

void BluetoothUiSession::enter_alias(UISetupPage &page)
{
    if (!require_power_enabled(page)) return;
    stop_scan_timer();
    stop_failure_feedback();
    action_operation_.abort(action_token_);
    action_busy_ = false;
    refresh_status(page);
    model_.begin_alias_edit();
    SetupPageAccess(page).set_view(SetupViewState::BT_ALIAS);
    build_alias_view(page);
}

void BluetoothUiSession::handle_alias_key(UISetupPage &page, uint32_t key)
{
    if (key == KEY_ESC || key == KEY_LEFT) {
        alias_input_lbl_ = nullptr;
        alias_hint_lbl_ = nullptr;
        SetupPageAccess access(page);
        access.play_back();
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
        return;
    }
    if (key == KEY_ENTER || key == KEY_RIGHT) {
        if (!require_power_enabled(page)) return;
        std::string alias = model_.sanitized_alias();
        if (alias_hint_lbl_) {
            lv_label_set_text(alias_hint_lbl_, "Setting alias...");
            lv_obj_set_style_text_color(alias_hint_lbl_, lv_color_hex(0xFFAA00), LV_PART_MAIN);
            lv_refr_now(NULL);
        }
        int ret = set_alias(alias);
        if (ret == 0) {
            model_.set_alias(alias);
            refresh_status(page);
            SetupPageAccess access(page);
            access.set_view(SetupViewState::SUB);
            access.build_sub_view();
        } else if (alias_hint_lbl_) {
            lv_label_set_text(alias_hint_lbl_, "Set failed");
            lv_obj_set_style_text_color(alias_hint_lbl_, lv_color_hex(0xFF4444), LV_PART_MAIN);
        }
        return;
    }
    if (key == KEY_BACKSPACE) {
        model_.erase_alias_character();
        alias_update_display();
        return;
    }
    const std::string_view input = SetupPageAccess(page).current_utf8();
    if (!input.empty()) {
        model_.append_alias_text(input.data());
        alias_update_display();
    }
}

void BluetoothUiSession::enter_scan(UISetupPage &page)
{
    if (!require_power_enabled(page)) return;
    stop_failure_feedback();
    action_operation_.abort(action_token_);
    action_busy_ = false;
    model_.set_list_mode(BluetoothListMode::SCAN);
    SetupPageAccess(page).set_view(SetupViewState::BT_LIST);
    start_scan_timer(page);
}

bool BluetoothUiSession::require_power_enabled(UISetupPage &page)
{
    if (get_status().powered != 0) return true;

    stop_scan_timer();
    stop_failure_feedback();
    action_operation_.abort(action_token_);
    action_busy_ = false;
    alias_input_lbl_ = nullptr;
    alias_hint_lbl_ = nullptr;
    refresh_status(page);

    SetupPageAccess access(page);
    access.set_view(SetupViewState::BT_POWER_WARNING);
    if (!show_power_warning(page)) {
        access.set_view(SetupViewState::SUB);
        access.select_sub(0, 6);
        access.build_sub_view();
    }
    return false;
}

void BluetoothUiSession::handle_power_warning_key(UISetupPage &page, uint32_t key)
{
    if (key != KEY_ENTER && key != KEY_ESC && key != KEY_LEFT) return;
    SetupPageAccess access(page);
    access.set_view(SetupViewState::SUB);
    access.select_sub(0, 6);
    access.build_sub_view();
}

void BluetoothUiSession::rebuild_rows()
{
    std::vector<BluetoothDeviceState> devices;
    devices.reserve(device_count_);
    for (int index = 0; index < device_count_; ++index) {
        const cp0_bt_device_t &device = devices_[index];
        devices.push_back({device.address, device.name, device.paired != 0, device.connected != 0});
    }
    model_.rebuild_rows(devices);
}

void BluetoothUiSession::activate_selected(UISetupPage &page)
{
    if (!require_power_enabled(page)) return;
    const bool operation_active = action_busy_ && action_operation_.active();
    if (!bluetooth_action_entry_allowed(action_busy_, operation_active)) return;
    if (action_busy_) action_busy_ = false;
    int dev_index = model_.selected_device_index();
    if (dev_index < 0)
        return;
    cp0_bt_device_t dev = devices_[dev_index];
    bool from_scan = model_.list_mode() == BluetoothListMode::SCAN;
    if (from_scan)
        suspend_scan_discovery();
    action_busy_ = true;
    if (dev.connected)
        show_action(page, "Disconnecting...");
    else if (dev.paired)
        show_action(page, "Connecting...");
    else
        show_action(page, "Pairing...");

    action_token_ = action_operation_.begin();
    AsyncOperationLifecycle::Token token = action_token_;
    if (!token) {
        action_busy_ = false;
        if (from_scan)
            resume_scan_discovery();
        return;
    }
    auto state = std::make_shared<ActionState>();
    action_state_ = state;
    action_page_ = &page;
    action_result_timer_ = lv_timer_create(action_result_timer_cb, 30, this);
    if (!action_result_timer_) {
        action_state_.reset();
        action_page_ = nullptr;
        action_operation_.abort(token);
        action_busy_ = false;
        if (from_scan) resume_scan_discovery();
        return;
    }
    try {
        if (!action_tasks_.start([state, dev, from_scan]() {
            int ret = -1;
            if (dev.connected) {
                ret = device_command("BtDisconnect", dev.address);
            } else if (dev.paired) {
                ret = device_command("BtConnect", dev.address);
            } else {
                ret = device_command("BtPair", dev.address);
                if (ret == 0)
                    ret = device_command("BtConnect", dev.address);
            }

            std::lock_guard<std::mutex> lock(state->mutex);
            state->result = ret;
            state->from_scan = from_scan;
            state->ready = true;
        })) {
            lv_timer_delete(action_result_timer_);
            action_result_timer_ = nullptr;
            action_state_.reset();
            action_page_ = nullptr;
            action_operation_.abort(token);
            action_busy_ = false;
            if (from_scan)
                resume_scan_discovery();
        }
    } catch (...) {
        if (action_result_timer_) {
            lv_timer_delete(action_result_timer_);
            action_result_timer_ = nullptr;
        }
        action_state_.reset();
        action_page_ = nullptr;
        action_operation_.abort(token);
        action_busy_ = false;
        if (from_scan)
            resume_scan_discovery();
    }
}

void BluetoothUiSession::action_result_timer_cb(lv_timer_t *timer) noexcept
{
    auto *bt = timer ? static_cast<BluetoothUiSession *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!bt || timer != bt->action_result_timer_ || !bt->action_state_) return;
    try {
        int result = -1;
        bool from_scan = false;
        {
            std::lock_guard<std::mutex> lock(bt->action_state_->mutex);
            if (!bt->action_state_->ready) return;
            result = bt->action_state_->result;
            from_scan = bt->action_state_->from_scan;
        }
        lv_timer_delete(bt->action_result_timer_);
        bt->action_result_timer_ = nullptr;
        bt->action_state_.reset();
        bt->action_tasks_.reap_finished();
        UISetupPage *page = bt->action_page_;
        bt->action_page_ = nullptr;
        if (!bluetooth_async_completion_allowed(bt->action_token_.complete(), page)) return;
        bt->action_busy_ = false;
        if (result != 0) {
            bt->show_action(*page, "Bluetooth action failed", 0xFF4444);
            if (bluetooth_scan_action_should_resume(from_scan, result))
                bt->resume_scan_discovery();
            // Do not synchronously query BlueZ while presenting the failure.
            // That query can block the LVGL thread and make ESC appear dead.
            return;
        } else if (from_scan) {
            bt->model_.set_list_mode(BluetoothListMode::MANAGED);
            bt->stop_scan_timer();
        }
        bt->refresh_devices();
        if (SetupPageAccess(*page).is_view(SetupViewState::BT_LIST))
            bt->build_list(*page);
    } catch (...) {
    }
}

void BluetoothUiSession::remove_selected(UISetupPage &page)
{
    if (!require_power_enabled(page)) return;
    int dev_index = model_.selected_device_index();
    if (dev_index < 0)
        return;
    show_action(page, "Removing...");
    int ret = device_command("BtRemove", devices_[dev_index].address);
    if (ret != 0) {
        show_action(page, "Remove failed", 0xFF4444);
        start_failure_feedback(page);
        return;
    }
    refresh_devices();
    build_list(page);
}

void BluetoothUiSession::start_failure_feedback(UISetupPage &page)
{
    stop_failure_feedback();
    if (!model_.begin_feedback()) return;
    failure_feedback_page_ = &page;
    failure_feedback_timer_ = lv_timer_create(failure_feedback_timer_cb, 1200, this);
    if (failure_feedback_timer_) {
        lv_timer_set_repeat_count(failure_feedback_timer_, 1);
        if (page.screen())
            lv_obj_add_event_cb(page.screen(), failure_feedback_screen_delete_cb,
                                LV_EVENT_DELETE, this);
        return;
    }

    failure_feedback_page_ = nullptr;
    model_.finish_feedback();
    refresh_devices();
    if (SetupPageAccess(page).is_view(SetupViewState::BT_LIST)) build_list(page);
}

void BluetoothUiSession::stop_failure_feedback()
{
    if (failure_feedback_page_ && failure_feedback_page_->screen())
        lv_obj_remove_event_cb_with_user_data(
            failure_feedback_page_->screen(), failure_feedback_screen_delete_cb, this);
    if (failure_feedback_timer_) {
        lv_timer_delete(failure_feedback_timer_);
        failure_feedback_timer_ = nullptr;
    }
    failure_feedback_page_ = nullptr;
    model_.cancel_feedback();
}

void BluetoothUiSession::failure_feedback_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = static_cast<BluetoothUiSession *>(lv_timer_get_user_data(timer));
    if (!self || !bluetooth_feedback_callback_allowed(
            timer, self->failure_feedback_timer_, self->failure_feedback_page_,
            self->model_.feedback_pending())) return;
    try {
        UISetupPage *page = self->failure_feedback_page_;
        if (page->screen())
            lv_obj_remove_event_cb_with_user_data(
                page->screen(), failure_feedback_screen_delete_cb, self);
        self->failure_feedback_timer_ = nullptr;
        self->failure_feedback_page_ = nullptr;
        if (!self->model_.finish_feedback()) return;
        if (!SetupPageAccess(*page).is_view(SetupViewState::BT_LIST)) return;
        self->refresh_devices();
        self->build_list(*page);
    } catch (...) {
        self->failure_feedback_timer_ = nullptr;
        self->failure_feedback_page_ = nullptr;
        self->model_.cancel_feedback();
    }
}

void BluetoothUiSession::failure_feedback_screen_delete_cb(lv_event_t *event) noexcept
{
    try {
        if (!event) return;
        auto *self = static_cast<BluetoothUiSession *>(lv_event_get_user_data(event));
        if (!self || !self->failure_feedback_page_ ||
            !bluetooth_feedback_screen_delete_matches(
                lv_event_get_target(event), lv_event_get_current_target(event),
                self->failure_feedback_page_->screen())) return;
        self->failure_feedback_page_ = nullptr;
        if (self->failure_feedback_timer_) {
            lv_timer_delete(self->failure_feedback_timer_);
            self->failure_feedback_timer_ = nullptr;
        }
        self->model_.cancel_feedback();
    } catch (...) {
        // Never let a C++ exception escape an LVGL C callback.
    }
}

void BluetoothUiSession::handle_list_key(UISetupPage &page, uint32_t key)
{
    if (model_.feedback_pending() && key != KEY_ESC && key != KEY_LEFT)
        return;
    switch (key) {
    case KEY_UP:
        model_.select_next_device(-1);
        build_list(page);
        break;
    case KEY_DOWN:
        model_.select_next_device(1);
        build_list(page);
        break;
    case KEY_ENTER:
        activate_selected(page);
        break;
    case KEY_D:
        if (model_.list_mode() == BluetoothListMode::MANAGED)
            remove_selected(page);
        break;
    case KEY_R:
        if (model_.list_mode() == BluetoothListMode::SCAN) {
            start_scan_timer(page);
        } else {
            refresh_devices();
            build_list(page);
        }
        break;
    case KEY_ESC:
    case KEY_LEFT: {
        stop_scan_timer();
        stop_failure_feedback();
        action_operation_.abort(action_token_);
        action_busy_ = false;
        SetupPageAccess access(page);
        refresh_status(page);
        access.set_view(SetupViewState::SUB);
        access.build_sub_view();
        break;
    }
    default:
        break;
    }
}

} // namespace setting
