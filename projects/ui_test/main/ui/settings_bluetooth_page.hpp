#pragma once

#include <algorithm>
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
#include "cp0_font_service.hpp"
#include "cp0_lvgl_app_page_assets.h"
#include "hal_lvgl_bsp.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "settings_bluetooth_api.hpp"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

struct Cp0BluetoothUiApiDispatch {
    using Handler = std::function<void(int, std::string)>;

    struct Result {
        std::weak_ptr<bool> lifetime;
        Handler handler;
        Handler stale_handler;
        uint64_t generation = 0;
        int code = -1;
        std::string data;
    };

    std::mutex mutex;
    bool stopped = false;
    std::deque<Result> pending;
};

inline void cp0_bluetooth_ui_enqueue(
    const std::shared_ptr<Cp0BluetoothUiApiDispatch> &dispatch,
    const std::weak_ptr<bool> &lifetime,
    Cp0BluetoothUiApiDispatch::Handler handler,
    Cp0BluetoothUiApiDispatch::Handler stale_handler,
    uint64_t generation,
    int code,
    std::string data)
{
    if (!dispatch) return;
    try {
        std::lock_guard<std::mutex> lock(dispatch->mutex);
        if (dispatch->stopped || lifetime.expired()) return;
        dispatch->pending.push_back({lifetime,
                                     std::move(handler),
                                     std::move(stale_handler),
                                     generation,
                                     code,
                                     std::move(data)});
    } catch (...) {
    }
}

enum class LvSettingBluetoothListMode { Connected, Scan };

class LvSettingBluetoothPage3 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        ScreenW     = 320,
        ScreenH     = 150,
        RowH        = 17,
        RowY        = 22,
        VisibleRows = 5,
        HintY       = ScreenH - 14,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }
    static constexpr int SCREEN_W     = 320;
    static constexpr int SCREEN_H     = 150;
    static constexpr int ROW_H        = 17;
    static constexpr int CONNECTED_ROW_Y = 22;
    static constexpr int SCAN_SECTION_Y  = 23;
    static constexpr int SCAN_ROW_Y      = 38;
    static constexpr int VISIBLE_ROWS = 5;
    static constexpr int HINT_Y       = SCREEN_H - 14;

    LvSettingBluetoothPage3() = default;

    LvSettingBluetoothPage3(lv_obj_t *parent,
                            const NodeIter &parent_node,
                            std::function<void()> back_callback,
                            LvSettingBluetoothListMode mode)
        : parent_node_(parent_node),
          mode_(mode)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
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

    ~LvSettingBluetoothPage3() override
    {
        if (keyboard_root_ && keyboard_event_dsc_) {
            lv_obj_remove_event_dsc(keyboard_root_, keyboard_event_dsc_);
            keyboard_event_dsc_ = nullptr;
        }
        if (api_timer_) {
            lv_timer_delete(api_timer_);
            api_timer_ = nullptr;
        }
        ++generation_;
        stop_scan();
        {
            std::lock_guard<std::mutex> lock(api_dispatch_->mutex);
            api_dispatch_->stopped = true;
            api_dispatch_->pending.clear();
        }
        page_lifetime_.reset();
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
        lv_obj_set_size(ComponensObj,
                        metric(LayoutMetric::ScreenW),
                        metric(LayoutMetric::ScreenH));
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
            std::bind(&LvSettingBluetoothPage3::handle_key_event,
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
        api_timer_ = lv_timer_create(api_result_timer_cb, 50, this);

        render();
        request_status();
    }

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
    };

    static void enqueue_api_result(const std::shared_ptr<ApiDispatchState> &dispatch,
                                   LvSettingBluetoothPage3 *owner,
                                   const std::weak_ptr<bool> &lifetime,
                                   const ApiHandler &handler,
                                   const ApiHandler &stale_handler,
                                   uint64_t generation,
                                   int code,
                                   std::string data) noexcept
    {
        if (!dispatch) return;
        try {
            std::lock_guard<std::mutex> lock(dispatch->mutex);
            if (dispatch->stopped) return;
            dispatch->pending.push_back(
                ApiResult{owner,
                          lifetime,
                          handler,
                          stale_handler,
                          generation,
                          code,
                          std::move(data)});
        } catch (...) {
        }
    }

    static void api_result_timer_cb(lv_timer_t *timer) noexcept
    {
        auto *self = timer
            ? static_cast<LvSettingBluetoothPage3 *>(lv_timer_get_user_data(timer))
            : nullptr;
        if (!self || timer != self->api_timer_) return;

        std::deque<ApiResult> pending;
        {
            self->api_tasks_.reap_finished();
            std::lock_guard<std::mutex> lock(self->api_dispatch_->mutex);
            pending.swap(self->api_dispatch_->pending);
        }

        const std::weak_ptr<bool> lifetime = self->page_lifetime_;
        for (auto &result : pending) {
            if (result.lifetime.expired() || result.owner != self || !self->ComponensObj)
                continue;
            if (result.generation != self->generation_) {
                try {
                    if (result.stale_handler) result.stale_handler(result.code, std::move(result.data));
                } catch (...) {
                }
                if (lifetime.expired()) break;
                continue;
            }

            try {
                if (result.handler)
                    result.handler(result.code, std::move(result.data));
            } catch (...) {
            }
            if (lifetime.expired()) break;
        }
    }

    void request_api(std::list<std::string> arguments,
                     ApiHandler handler,
                     ApiHandler stale_handler = {})
    {
        const std::weak_ptr<bool> lifetime = page_lifetime_;
        const uint64_t request_generation = generation_;
        const auto dispatch = api_dispatch_;
        const ApiHandler fallback_handler = handler;
        const ApiHandler fallback_stale_handler = stale_handler;
        auto callback = [dispatch,
                         owner = this,
                         lifetime,
                         request_generation,
                         handler = std::move(handler),
                         stale_handler = std::move(stale_handler)](int code,
                                                                     std::string data) mutable {
            enqueue_api_result(dispatch,
                               owner,
                               lifetime,
                               handler,
                               stale_handler,
                               request_generation,
                               code,
                               std::move(data));
        };

        const bool started = api_tasks_.start(
            [arguments = std::move(arguments), callback = std::move(callback)]() mutable {
                try {
                    cp0_signal_bt_api(std::move(arguments), callback);
                } catch (...) {
                    callback(-1, "Bluetooth service unavailable");
                }
            });
        if (!started) {
            enqueue_api_result(dispatch,
                               this,
                               lifetime,
                               fallback_handler,
                               fallback_stale_handler,
                               request_generation,
                               -1,
                               "Bluetooth request could not be scheduled");
        }
    }

    static bool decode_status(const std::string &data,
                              bool &powered,
                              std::string &address)
    {
        settings_bluetooth::StatusRecord decoded;
        if (!settings_bluetooth::decode_status(data, decoded)) return false;
        powered = decoded.powered;
        address = std::move(decoded.address);
        return true;
    }

    static std::string device_text(const char *value, size_t size)
    {
        if (!value || size == 0) return {};
        size_t length = 0;
        while (length < size && value[length]) ++length;
        return std::string(value, length);
    }

    static void copy_text(char *target, size_t capacity, std::string_view value)
    {
        if (!target || capacity == 0) return;
        const size_t length = std::min(capacity - 1, value.size());
        if (length > 0) std::memcpy(target, value.data(), length);
        target[length] = '\0';
    }

    static void copy_device_record(const settings_bluetooth::DeviceRecord &source,
                                   cp0_bt_device_t &target)
    {
        target = {};
        copy_text(target.address, sizeof(target.address), source.address);
        copy_text(target.name, sizeof(target.name), source.name);
        target.rssi = source.rssi;
        target.connected = source.connected ? 1 : 0;
        target.paired = source.paired ? 1 : 0;
        target.trusted = source.trusted ? 1 : 0;
    }

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
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        return label;
    }

    void request_status()
    {
        if (status_pending_) return;
        status_pending_ = true;
        request_api(settings_bluetooth::status_request(), [this](int code, std::string data) {
            status_pending_ = false;
            const bool decoded = code == 0 && decode_status(data, powered_, adapter_address_);
            if (!decoded) {
                ++generation_;
                status_known_ = false;
                powered_ = false;
                adapter_address_.clear();
                devices_.clear();
                clamp_selection();
                error_message_ = "Bluetooth service unavailable.";
                stop_scan();
                render();
                return;
            }

            status_known_ = true;
            error_message_.clear();
            if (!powered_) {
                ++generation_;
                devices_.clear();
                clamp_selection();
                stop_scan();
                render();
                show_power_warning();
                return;
            }
            render();
            if (mode_ == LvSettingBluetoothListMode::Scan)
                start_scan();
            else
                refresh_devices();
        }, [this](int, std::string) {
            status_pending_ = false;
        });
    }

    void refresh_devices()
    {
        if (!status_known_ || !powered_ || action_pending_ || list_pending_) return;
        list_pending_ = true;
        loading_ = true;
        render();

        std::list<std::string> request;
        if (!settings_bluetooth::list_request(
                mode_ == LvSettingBluetoothListMode::Connected,
                CP0_BT_DEVICE_MAX,
                request)) {
            list_pending_ = false;
            loading_ = false;
            error_message_ = "Bluetooth device list unavailable.";
            render();
            return;
        }
        request_api(std::move(request),
                    [this](int code, std::string data) {
                        list_pending_ = false;
                        loading_ = false;
                        if (!status_known_ || !powered_) {
                            devices_.clear();
                            error_message_ = "Bluetooth is off. Enable Power first.";
                        } else {
                            std::vector<settings_bluetooth::DeviceRecord> decoded_devices;
                            if (!settings_bluetooth::decode_device_list_reply(
                                    code, data, decoded_devices, CP0_BT_DEVICE_MAX)) {
                                devices_.clear();
                                error_message_ = "Bluetooth device list unavailable.";
                            } else {
                                devices_.clear();
                                devices_.reserve(decoded_devices.size());
                                for (const auto &device : decoded_devices) {
                                    cp0_bt_device_t converted{};
                                    copy_device_record(device, converted);
                                    devices_.push_back(converted);
                                }
                                error_message_.clear();
                            }
                        }
                        clamp_selection();
                        render();
                    });
    }

    void start_scan()
    {
        if (mode_ != LvSettingBluetoothListMode::Scan || !powered_ ||
            scan_start_pending_ || scan_stop_pending_ || discovery_active_)
            return;

        scan_start_pending_ = true;
        loading_ = true;
        error_message_.clear();
        render();
        const auto complete_start = [this](int code, std::string data) {
            scan_start_pending_ = false;
            if (scan_stop_after_start_) {
                scan_stop_after_start_ = false;
                const auto continuation = std::move(scan_after_stop_);
                scan_after_stop_ = nullptr;
                if (!settings_bluetooth::success_without_payload(code, data)) {
                    discovery_active_ = false;
                    loading_ = false;
                    if (continuation) continuation();
                    return;
                }
                discovery_active_ = true;
                stop_scan(continuation);
                return;
            }
            if (!settings_bluetooth::success_without_payload(code, data) || !powered_) {
                discovery_active_ = false;
                error_message_ = powered_
                    ? "Bluetooth scan unavailable."
                    : "Bluetooth is off. Enable Power first.";
                loading_ = false;
                render();
                return;
            }

            discovery_active_ = true;
            if (!scan_timer_)
                scan_timer_ = lv_timer_create(scan_timer_cb, 1500, this);
            if (!scan_timer_) {
                error_message_ = "Bluetooth scan unavailable.";
                stop_scan();
                render();
                return;
            }
            refresh_devices();
        };
        request_api(settings_bluetooth::discovery_start_request(),
                    complete_start,
                    complete_start);
    }

    void restart_scan()
    {
        if (mode_ != LvSettingBluetoothListMode::Scan || !powered_ || action_pending_)
            return;

        ++generation_;
        list_pending_ = false;
        stop_scan([this] {
            if (status_known_ && powered_ && !action_pending_) start_scan();
        });
    }

    void stop_scan(std::function<void()> after_stop = {})
    {
        if (scan_timer_) {
            lv_timer_delete(scan_timer_);
            scan_timer_ = nullptr;
        }
        const bool needs_stop = discovery_active_ || scan_start_pending_;
        if (scan_start_pending_) {
            scan_stop_after_start_ = true;
            if (after_stop) {
                auto previous = std::move(scan_after_stop_);
                scan_after_stop_ = [previous = std::move(previous),
                                    after_stop = std::move(after_stop)]() mutable {
                    if (previous) previous();
                    if (after_stop) after_stop();
                };
            }
            discovery_active_ = false;
            loading_ = false;
            return;
        }
        discovery_active_ = false;
        scan_start_pending_ = false;
        loading_ = false;

        if (scan_stop_pending_) {
            if (after_stop) {
                auto previous = std::move(scan_after_stop_);
                scan_after_stop_ = [previous = std::move(previous),
                                    after_stop = std::move(after_stop)]() mutable {
                    if (previous) previous();
                    if (after_stop) after_stop();
                };
            }
            return;
        }

        if (!needs_stop) {
            if (after_stop) after_stop();
            return;
        }

        scan_stop_pending_ = true;
        scan_after_stop_ = std::move(after_stop);
        const uint64_t stop_request_id = ++scan_stop_request_id_;
        const auto complete_stop = [this, stop_request_id](int code,
                                                           std::string data) {
            if (!scan_stop_pending_ || stop_request_id != scan_stop_request_id_) return;
            scan_stop_pending_ = false;
            const auto continuation = std::move(scan_after_stop_);
            scan_after_stop_ = nullptr;
            if (!settings_bluetooth::success_without_payload(code, data)) {
                error_message_ = "Bluetooth scan stop failed.";
                if (action_waiting_for_scan_stop_) {
                    action_waiting_for_scan_stop_ = false;
                    cancel_action();
                }
                render();
                return;
            }
            if (continuation) continuation();
        };
        request_api(settings_bluetooth::discovery_stop_request(),
                    complete_stop,
                    complete_stop);
    }

    static void scan_timer_cb(lv_timer_t *timer) noexcept
    {
        auto *self = timer
            ? static_cast<LvSettingBluetoothPage3 *>(lv_timer_get_user_data(timer))
            : nullptr;
        if (!self || self->scan_timer_ != timer ||
            !self->discovery_active_ || self->action_pending_)
            return;
        self->refresh_devices();
    }

    void submit_action(bool connected, bool paired)
    {
        if (!action_pending_) return;
        action_waiting_for_scan_stop_ = false;
        if (connected) {
            request_action("BtDisconnect", action_address_);
        } else if (paired) {
            request_action("BtConnect", action_address_);
        } else {
            std::list<std::string> request;
            if (!settings_bluetooth::device_request("BtPair", action_address_, request)) {
                finish_action(-1, "");
                return;
            }
            request_api(std::move(request), [this](int code, std::string data) {
                if (!settings_bluetooth::success_without_payload(code, data)) {
                    finish_action(code, std::move(data));
                    return;
                }
                action_message_ = "Connecting...";
                render();
                request_action("BtConnect", action_address_);
            }, [this](int, std::string) {
                cancel_action();
                render();
            });
        }
    }

    void activate_selected()
    {
        if (action_pending_ || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(devices_.size()) || !powered_)
            return;

        const cp0_bt_device_t device = devices_[static_cast<size_t>(selected_index_)];
        const std::string address = device_text(device.address, sizeof(device.address));
        if (!settings_bluetooth::valid_device_address(address)) {
            error_message_ = "Bluetooth device address is invalid.";
            render();
            return;
        }

        action_pending_ = true;
        action_address_ = address;
        ++generation_;
        list_pending_ = false;
        action_message_ = device.connected ? "Disconnecting..."
            : device.paired ? "Connecting..." : "Pairing...";
        render();

        const bool connected = device.connected != 0;
        const bool paired = device.paired != 0;
        const auto submit = [this, connected, paired] {
            submit_action(connected, paired);
        };
        if (mode_ == LvSettingBluetoothListMode::Scan) {
            action_waiting_for_scan_stop_ = discovery_active_ ||
                scan_start_pending_ || scan_stop_pending_;
            if (action_waiting_for_scan_stop_)
                stop_scan(submit);
            else {
                stop_scan();
                submit();
            }
        } else {
            submit();
        }
    }

    void request_action(const char *command, const std::string &address)
    {
        std::list<std::string> request;
        if (!settings_bluetooth::device_request(command, address, request)) {
            finish_action(-1, "");
            return;
        }
        request_api(std::move(request), [this](int code, std::string data) {
            finish_action(code, std::move(data));
        }, [this](int, std::string) {
            cancel_action();
            render();
        });
    }

    void cancel_action()
    {
        action_waiting_for_scan_stop_ = false;
        action_pending_ = false;
        action_message_.clear();
        action_address_.clear();
    }

    void finish_action(int code, std::string data)
    {
        const bool succeeded = settings_bluetooth::success_without_payload(code, data);
        cancel_action();
        if (!succeeded) {
            error_message_ = "Bluetooth action failed.";
            render();
            if (mode_ == LvSettingBluetoothListMode::Scan)
                restart_scan();
            return;
        }

        error_message_.clear();
        if (mode_ == LvSettingBluetoothListMode::Scan) {
            restart_scan();
        } else {
            render();
            refresh_devices();
        }
    }

    void remove_selected()
    {
        if (mode_ != LvSettingBluetoothListMode::Connected || action_pending_ ||
            selected_index_ < 0 || selected_index_ >= static_cast<int>(devices_.size()))
            return;
        const std::string address = device_text(
            devices_[static_cast<size_t>(selected_index_)].address,
            sizeof(devices_[static_cast<size_t>(selected_index_)].address));
        if (!settings_bluetooth::valid_device_address(address)) {
            error_message_ = "Bluetooth device address is invalid.";
            render();
            return;
        }
        action_pending_ = true;
        ++generation_;
        list_pending_ = false;
        action_message_ = "Removing...";
        render();
        request_action("BtRemove", address);
    }

    void clamp_selection()
    {
        if (devices_.empty()) {
            selected_index_ = 0;
            return;
        }
        selected_index_ = std::clamp(
            selected_index_, 0, static_cast<int>(devices_.size()) - 1);
    }

    bool move_selection(int direction)
    {
        if (action_pending_ || devices_.empty() || direction == 0) return false;
        const int next = std::clamp(
            selected_index_ + direction,
            0,
            static_cast<int>(devices_.size()) - 1);
        if (next == selected_index_) return false;
        selected_index_ = next;
        return true;
    }

    void render()
    {
        if (!ComponensObj) return;
        if (warning_active_) return;
        lv_obj_clean(ComponensObj);

        std::string title = mode_ == LvSettingBluetoothListMode::Scan
            ? "Scan"
            : "Connected";
        title += ": ";
        if (!status_known_)
            title += "Checking...";
        else
            title += powered_ ? "On" : "Off";
        if (!adapter_address_.empty()) {
            title += "  ";
            title += adapter_address_;
        }
        create_label(
            ComponensObj,
            title.c_str(),
            8,
            2,
            metric(LayoutMetric::ScreenW) - 16,
            0x58A6FF,
            cp0_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD));

        const char *hint = mode_ == LvSettingBluetoothListMode::Scan
            ? "OK:act  R:restart  ESC:back"
            : "OK:toggle  D:remove  ESC:back";
        if (action_pending_) {
            create_label(ComponensObj,
                         action_message_.c_str(),
                         8,
                         mode_ == LvSettingBluetoothListMode::Scan ? 45 : 52,
                         SCREEN_W - 16,
                         0x58A6FF,
                         &lv_font_montserrat_14);
            hint = "ESC:back";
        } else {
            std::string message;
            if (!status_known_)
                message = "Checking Bluetooth status...";
            else if (!powered_)
                message = "Bluetooth is off. Enable Power first.";
            else if (!error_message_.empty())
                message = error_message_;
            else if (devices_.empty())
                message = mode_ == LvSettingBluetoothListMode::Scan
                    ? (loading_ ? "Scanning..." : "No devices found.")
                    : "No connected devices.";

            if (!message.empty()) {
                create_label(ComponensObj,
                             message.c_str(),
                             8,
                             mode_ == LvSettingBluetoothListMode::Scan ? 45 : 52,
                             SCREEN_W - 16,
                             error_message_.empty() ? 0x666666 : 0xFFAA00,
                             &lv_font_montserrat_12);
            }

            if (mode_ == LvSettingBluetoothListMode::Scan) {
                create_label(ComponensObj,
                             "Discovered Devices",
                             8,
                             SCAN_SECTION_Y,
                             SCREEN_W - 16,
                             0x888888,
                             &lv_font_montserrat_10);
            }

            const int count = static_cast<int>(devices_.size());
            const int offset = count <= metric(LayoutMetric::VisibleRows)
                ? 0
                : std::clamp(selected_index_ - metric(LayoutMetric::VisibleRows) / 2,
                             0,
                             count - metric(LayoutMetric::VisibleRows));
            for (int visible = 0;
                 visible < metric(LayoutMetric::VisibleRows) && offset + visible < count;
                 ++visible) {
                const int index = offset + visible;
                const auto &device = devices_[static_cast<size_t>(index)];
                const int y = (mode_ == LvSettingBluetoothListMode::Scan
                                   ? SCAN_ROW_Y
                                   : CONNECTED_ROW_Y) + visible * ROW_H;
                const bool selected = index == selected_index_;
                if (selected) {
                    lv_obj_t *background = lv_obj_create(ComponensObj);
                    if (background) {
                        lv_obj_set_size(background,
                                        metric(LayoutMetric::ScreenW) - 8,
                                        metric(LayoutMetric::RowH) - 1);
                        lv_obj_set_pos(background, 4, y);
                        lv_obj_set_style_radius(background, 2, LV_PART_MAIN);
                        lv_obj_set_style_bg_color(background, lv_color_hex(0x1F3A5F), LV_PART_MAIN);
                        lv_obj_set_style_bg_opa(background, LV_OPA_COVER, LV_PART_MAIN);
                        lv_obj_set_style_border_width(background, 0, LV_PART_MAIN);
                        lv_obj_remove_flag(background, LV_OBJ_FLAG_CLICKABLE);
                        lv_obj_remove_flag(background, LV_OBJ_FLAG_SCROLLABLE);
                    }
                }

                const uint32_t color = device.connected
                    ? 0x58A6FF
                    : selected ? 0xFFFFFF : 0xCCCCCC;
                const std::string name = device_text(device.name, sizeof(device.name));
                const std::string address = device_text(device.address, sizeof(device.address));
                create_label(ComponensObj,
                             name.empty() ? address.c_str() : name.c_str(),
                             8,
                             y + 1,
                             208,
                             color,
                             &lv_font_montserrat_10);
                create_label(ComponensObj,
                             address.c_str(),
                             8,
                             y + 10,
                             214,
                             selected ? 0xBBBBBB : 0x777777,
                             &lv_font_montserrat_8);

                std::string state;
                if (device.connected)
                    state = "Connected";
                else if (device.paired)
                    state = "Paired";
                else
                    state = std::to_string(device.rssi);
                lv_obj_t *state_label = create_label(
                    ComponensObj,
                    state.c_str(),
                    232,
                    y + 2,
                    80,
                    color,
                    &lv_font_montserrat_8);
                if (state_label)
                    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            }
        }

        create_label(ComponensObj,
                     hint,
                     8,
                     metric(LayoutMetric::HintY),
                     metric(LayoutMetric::ScreenW) - 16,
                     0x555555,
                     &lv_font_montserrat_10);
    }

    void show_power_warning()
    {
        if (!ComponensObj || warning_active_) return;
        warning_active_ = true;
        lv_obj_clean(ComponensObj);

        lv_obj_t *overlay = lv_obj_create(ComponensObj);
        if (!overlay) return;
        lv_obj_set_size(overlay, SCREEN_W, SCREEN_H);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

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
        lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

        create_label(dialog, "Bluetooth power is off", 12, 10, 250,
                     0xFFAA00, &lv_font_montserrat_14);
        create_label(dialog, "Turn on Power before continuing.", 12, 36, 250,
                     0xCCCCCC, &lv_font_montserrat_12);
        create_label(dialog, "OK", 246, 68, 28,
                     0x58A6FF, &lv_font_montserrat_12);
    }

    static void keyboard_event_cb(lv_event_t *event)
    {
        if (!event) return;
        auto *self = static_cast<LvSettingBluetoothPage3 *>(lv_event_get_user_data(event));
        auto *item = static_cast<const key_item *>(lv_event_get_param(event));
        if (!self || !item || item->key_state != KBD_KEY_PRESSED) return;

        if (self->mode_ == LvSettingBluetoothListMode::Scan &&
            (item->key_code == KEY_R || item->semantic_key == KEY_R)) {
            self->restart_scan();
        } else if (self->mode_ == LvSettingBluetoothListMode::Connected &&
                   (item->key_code == KEY_D || item->semantic_key == KEY_D)) {
            self->remove_selected();
        }
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        const uint32_t key = lv_event_get_key(event);
        if (warning_active_) {
            if (key == LV_KEY_ESC || key == LV_KEY_LEFT || key == LV_KEY_ENTER ||
                key == LV_KEY_RIGHT) {
                warning_active_ = false;
                if (LeaveSelfPage) LeaveSelfPage();
            }
            lv_event_stop_processing(event);
            return;
        }
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            ++generation_;
            stop_scan();
            if (LeaveSelfPage) LeaveSelfPage();
        } else if (key == LV_KEY_UP) {
            if (move_selection(-1)) render();
        } else if (key == LV_KEY_DOWN) {
            if (move_selection(1)) render();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            activate_selected();
        } else if (key == LV_KEY_DEL || key == LV_KEY_NEXT) {
            if (mode_ == LvSettingBluetoothListMode::Scan)
                restart_scan();
            else
                remove_selected();
        } else if (key == 'r' || key == 'R') {
            if (mode_ == LvSettingBluetoothListMode::Scan) restart_scan();
        } else if (key == 'd' || key == 'D') {
            if (mode_ == LvSettingBluetoothListMode::Connected) remove_selected();
        }
        lv_event_stop_processing(event);
    }

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
    bool action_waiting_for_scan_stop_ = false;
    bool scan_start_pending_ = false;
    bool scan_stop_after_start_ = false;
    bool scan_stop_pending_ = false;
    bool discovery_active_ = false;
    std::string adapter_address_;
    std::string error_message_;
    std::string action_message_;
    std::string action_address_;
    std::function<void()> scan_after_stop_;
    uint64_t scan_stop_request_id_ = 0;
    lv_timer_t *scan_timer_ = nullptr;
    lv_timer_t *api_timer_ = nullptr;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
    bool warning_active_ = false;
    Cp0BoundedTaskRegistry api_tasks_;
    std::shared_ptr<ApiDispatchState> api_dispatch_ = std::make_shared<ApiDispatchState>();
    std::shared_ptr<bool> page_lifetime_ = std::make_shared<bool>(true);
    uint64_t generation_ = 1;
};
class LvSettingBluetoothAliasPage3 : public DComponens::LvglComponensBase {
public:
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 150;
    static constexpr std::size_t MAX_ALIAS_BYTES = CP0_BT_NAME_MAX - 1;
    static constexpr int ALIAS_TEXT_X = 64;
    static constexpr int ALIAS_TEXT_RIGHT_INSET = 8;
    static constexpr int CURSOR_GAP = 2;
    static constexpr int CURSOR_WIDTH = 2;
    static constexpr int CURSOR_HEIGHT = 18;

    LvSettingBluetoothAliasPage3() = default;

    LvSettingBluetoothAliasPage3(lv_obj_t *parent,
                                 const NodeIter &parent_node,
                                 std::function<void()> back_callback,
                                 std::string initial_alias,
                                 std::function<void(std::string)> saved_callback)
        : parent_node_(parent_node),
          alias_(std::move(initial_alias)),
          saved_callback_(std::move(saved_callback))
    {
        LeaveSelfPage = std::move(back_callback);
        if (!settings_bluetooth::valid_alias(alias_)) alias_ = "CardputerZero";
        backend_alias_ = alias_;
        cursor_ = alias_.size();
        create_ui(parent);
    }

    ~LvSettingBluetoothAliasPage3() override
    {
        if (keyboard_root_ && keyboard_event_dsc_) {
            lv_obj_remove_event_dsc(keyboard_root_, keyboard_event_dsc_);
            keyboard_event_dsc_ = nullptr;
        }
        if (api_timer_) {
            lv_timer_delete(api_timer_);
            api_timer_ = nullptr;
        }
        if (cursor_timer_) {
            lv_timer_delete(cursor_timer_);
            cursor_timer_ = nullptr;
        }
        ++generation_;
        {
            std::lock_guard<std::mutex> lock(api_dispatch_->mutex);
            api_dispatch_->stopped = true;
            api_dispatch_->pending.clear();
        }
        lifetime_.reset();
        api_tasks_.join_all();
        if (ComponensObj) {
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
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
            std::bind(&LvSettingBluetoothAliasPage3::handle_key_event,
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
        api_timer_ = lv_timer_create(api_result_timer_cb, 50, this);
        cursor_timer_ = lv_timer_create(cursor_timer_cb, 500, this);
        render();
        status_pending_ = true;
        request_api(settings_bluetooth::status_request(), [this](int code, std::string data) {
            status_pending_ = false;
            settings_bluetooth::StatusRecord status;
            status_known_ = code == 0 && settings_bluetooth::decode_status(data, status);
            if (status_known_) {
                powered_ = status.powered;
                if (!status.alias.empty()) {
                    alias_ = status.alias;
                    backend_alias_ = status.alias;
                    cursor_ = alias_.size();
                }
            } else {
                powered_ = false;
            }
            if (!status_known_) {
                error_message_ = "Bluetooth service unavailable.";
                render();
                return;
            }
            if (!powered_) {
                show_power_warning();
                return;
            }
            render();
        }, [this](int, std::string) {
            status_pending_ = false;
        });
    }

private:
    using ApiHandler = std::function<void(int, std::string)>;

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
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
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

    void render()
    {
        if (!ComponensObj) return;
        if (warning_active_) return;
        lv_obj_clean(ComponensObj);
        create_label(ComponensObj,
                     "Bluetooth Name",
                     8,
                     8,
                     SCREEN_W - 16,
                     0x58A6FF,
                     cp0_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD));
        create_label(ComponensObj, "Name:", 8, 38, 52, 0xCCCCCC, &lv_font_montserrat_12);

        const bool show_cursor = !saving_ && cursor_visible_;
        if (!show_cursor) {
            create_label(ComponensObj,
                         alias_.c_str(),
                         ALIAS_TEXT_X,
                         36,
                         SCREEN_W - ALIAS_TEXT_X - ALIAS_TEXT_RIGHT_INSET,
                         0xFFFFFF,
                         &lv_font_montserrat_14);
        } else {
            const std::string prefix = alias_.substr(0, cursor_);
            const std::string suffix = alias_.substr(cursor_);
            const int field_right = SCREEN_W - ALIAS_TEXT_RIGHT_INSET;
            const int max_prefix_width = std::max(
                0,
                field_right - ALIAS_TEXT_X - CURSOR_GAP - CURSOR_WIDTH - CURSOR_GAP);
            lv_obj_t *prefix_label = create_label(ComponensObj,
                                                  prefix.c_str(),
                                                  ALIAS_TEXT_X,
                                                  36,
                                                  0,
                                                  0xFFFFFF,
                                                  &lv_font_montserrat_14);
            if (prefix_label) lv_obj_update_layout(prefix_label);
            const int measured_prefix_width =
                prefix_label ? lv_obj_get_width(prefix_label) : 0;
            const int prefix_width = std::min(measured_prefix_width, max_prefix_width);
            if (prefix_label && measured_prefix_width > max_prefix_width) {
                lv_obj_set_width(prefix_label, max_prefix_width);
                lv_label_set_long_mode(prefix_label, LV_LABEL_LONG_CLIP);
            }
            const int cursor_x = ALIAS_TEXT_X + prefix_width + CURSOR_GAP;
            lv_obj_t *cursor_bar = lv_obj_create(ComponensObj);
            if (cursor_bar) {
                lv_obj_set_size(cursor_bar, CURSOR_WIDTH, CURSOR_HEIGHT);
                lv_obj_set_pos(cursor_bar, cursor_x, 34);
                lv_obj_set_style_bg_color(cursor_bar, lv_color_hex(0x58A6FF), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(cursor_bar, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(cursor_bar, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(cursor_bar, 0, LV_PART_MAIN);
                lv_obj_clear_flag(cursor_bar, LV_OBJ_FLAG_CLICKABLE);
            }
            const int suffix_x = cursor_x + CURSOR_WIDTH + CURSOR_GAP;
            create_label(ComponensObj,
                         suffix.c_str(),
                         suffix_x,
                         36,
                         std::max(1, field_right - suffix_x),
                         0xFFFFFF,
                         &lv_font_montserrat_14);
        }

        const char *hint = saving_ ? "Setting alias..." : "OK:set  BS:del  ESC:cancel";
        create_label(ComponensObj,
                     hint,
                     8,
                     SCREEN_H - 14,
                     SCREEN_W - 16,
                     saving_ ? 0xFFAA00 : 0x555555,
                     &lv_font_montserrat_10);
        if (!error_message_.empty())
            create_label(ComponensObj,
                         error_message_.c_str(),
                         8,
                         76,
                         SCREEN_W - 16,
                         0xFF4444,
                         &lv_font_montserrat_10);
    }

    void show_power_warning()
    {
        if (!ComponensObj || warning_active_) return;
        warning_active_ = true;
        lv_obj_clean(ComponensObj);
        lv_obj_t *overlay = lv_obj_create(ComponensObj);
        if (!overlay) return;
        lv_obj_set_size(overlay, SCREEN_W, SCREEN_H);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *dialog = lv_msgbox_create(overlay);
        if (!dialog) {
            warning_active_ = false;
            render();
            return;
        }
        lv_obj_set_size(dialog, 280, 92);
        lv_obj_center(dialog);
        lv_obj_set_style_radius(dialog, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(dialog, lv_color_hex(0xFFAA00), LV_PART_MAIN);
        lv_obj_set_style_bg_color(dialog, lv_color_hex(0x171717), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(dialog, 0, LV_PART_MAIN);
        lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *title = lv_msgbox_add_title(dialog, "Bluetooth power is off");
        lv_obj_t *header = lv_msgbox_get_header(dialog);
        lv_obj_t *content = lv_msgbox_get_content(dialog);
        lv_obj_t *message = lv_msgbox_add_text(dialog, "Turn on Power before continuing.");
        lv_obj_t *ok_button = lv_msgbox_add_footer_button(dialog, "OK");
        lv_obj_t *footer = lv_msgbox_get_footer(dialog);
        lv_obj_t *ok_label = ok_button ? lv_obj_get_child(ok_button, 0) : nullptr;

        if (header) {
            lv_obj_set_height(header, 30);
            lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_left(header, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_right(header, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_top(header, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(header, 0, LV_PART_MAIN);
        }
        if (title) {
            lv_obj_set_style_text_color(title, lv_color_hex(0xFFAA00), LV_PART_MAIN);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        }
        if (content) {
            lv_obj_set_height(content, 32);
            lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_left(content, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_right(content, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_top(content, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(content, 0, LV_PART_MAIN);
            lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        }
        if (message) {
            lv_obj_set_style_text_color(message, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
            lv_obj_set_style_text_font(message, &lv_font_montserrat_12, LV_PART_MAIN);
            lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        }
        if (footer) {
            lv_obj_set_height(footer, 28);
            lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(footer, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_left(footer, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_right(footer, 6, LV_PART_MAIN);
            lv_obj_set_style_pad_top(footer, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(footer, 0, LV_PART_MAIN);
            lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        }
        if (ok_button) {
            lv_obj_set_width(ok_button, 28);
            lv_obj_set_height(ok_button, 22);
            lv_obj_set_style_bg_opa(ok_button, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(ok_button, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(ok_button, 0, LV_PART_MAIN);
        }
        if (ok_label) {
            lv_obj_set_style_text_color(ok_label, lv_color_hex(0x58A6FF), LV_PART_MAIN);
            lv_obj_set_style_text_font(ok_label, &lv_font_montserrat_12, LV_PART_MAIN);
            lv_obj_set_style_text_align(ok_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        }
    }

    void request_api(std::list<std::string> arguments,
                     ApiHandler handler,
                     ApiHandler stale_handler = {})
    {
        const auto dispatch = api_dispatch_;
        const auto lifetime = lifetime_;
        const uint64_t request_generation = generation_;
        const ApiHandler fallback_handler = handler;
        const ApiHandler fallback_stale_handler = stale_handler;
        auto callback = [dispatch,
                         lifetime,
                         request_generation,
                         handler = std::move(handler),
                         stale_handler = std::move(stale_handler)](
                            int code, std::string data) mutable {
            cp0_bluetooth_ui_enqueue(dispatch,
                                     lifetime,
                                     std::move(handler),
                                     std::move(stale_handler),
                                     request_generation,
                                     code,
                                     std::move(data));
        };
        const bool started = api_tasks_.start(
            [arguments = std::move(arguments), callback = std::move(callback)]() mutable {
                try {
                    cp0_signal_bt_api(std::move(arguments), callback);
                } catch (...) {
                    callback(-1, "Bluetooth service unavailable");
                }
            });
        if (!started)
            cp0_bluetooth_ui_enqueue(dispatch,
                                     lifetime,
                                     fallback_handler,
                                     fallback_stale_handler,
                                     request_generation,
                                     -1,
                                     "Bluetooth request could not be scheduled");
    }

    static void api_result_timer_cb(lv_timer_t *timer) noexcept
    {
        auto *self = timer
            ? static_cast<LvSettingBluetoothAliasPage3 *>(lv_timer_get_user_data(timer))
            : nullptr;
        if (!self || timer != self->api_timer_) return;
        self->api_tasks_.reap_finished();
        std::deque<Cp0BluetoothUiApiDispatch::Result> pending;
        {
            std::lock_guard<std::mutex> lock(self->api_dispatch_->mutex);
            pending.swap(self->api_dispatch_->pending);
        }
        const std::weak_ptr<bool> lifetime = self->lifetime_;
        for (auto &result : pending) {
            if (result.lifetime.expired()) continue;
            try {
                if (result.generation != self->generation_) {
                    if (result.stale_handler)
                        result.stale_handler(result.code, std::move(result.data));
                } else if (result.handler) {
                    result.handler(result.code, std::move(result.data));
                }
            } catch (...) {
            }
            if (lifetime.expired()) break;
        }
    }

    static void cursor_timer_cb(lv_timer_t *timer) noexcept
    {
        auto *self = timer
            ? static_cast<LvSettingBluetoothAliasPage3 *>(lv_timer_get_user_data(timer))
            : nullptr;
        if (!self || timer != self->cursor_timer_ || self->warning_active_ || self->saving_)
            return;
        self->cursor_visible_ = !self->cursor_visible_;
        self->render();
    }

    void append_text(const char *text)
    {
        if (saving_ || !text || !text[0]) return;
        const std::string_view input(text);
        if (!settings_bluetooth::valid_text_field(input, MAX_ALIAS_BYTES, false) ||
            alias_.size() + input.size() > MAX_ALIAS_BYTES)
            return;
        const std::size_t length = input.size();
        alias_.insert(cursor_, text, length);
        cursor_ += length;
        cursor_visible_ = true;
        error_message_.clear();
        render();
    }

    void save()
    {
        if (saving_) return;
        if (!settings_bluetooth::valid_alias(alias_)) {
            error_message_ = "Name must be 1-63 UTF-8 bytes.";
            render();
            return;
        }
        if (!status_known_ || !powered_) {
            show_power_warning();
            return;
        }
        std::list<std::string> request;
        if (!settings_bluetooth::alias_request(alias_, request)) {
            error_message_ = "Name contains unsupported characters.";
            render();
            return;
        }
        alias_before_save_ = backend_alias_;
        saving_ = true;
        error_message_.clear();
        render();
        request_api(std::move(request), [this](int code, std::string data) {
            saving_ = false;
            if (!settings_bluetooth::success_without_payload(code, data)) {
                alias_ = alias_before_save_;
                cursor_ = std::min(cursor_, alias_.size());
                error_message_ = "Set alias failed.";
                render();
                return;
            }
            backend_alias_ = alias_;
            if (saved_callback_) saved_callback_(alias_);
            if (LeaveSelfPage) LeaveSelfPage();
        }, [this](int, std::string) {
            saving_ = false;
            alias_ = alias_before_save_;
            cursor_ = std::min(cursor_, alias_.size());
            error_message_ = "Set alias cancelled.";
            render();
        });
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        const uint32_t key = lv_event_get_key(event);
        if (warning_active_) {
            if (key == LV_KEY_ESC || key == LV_KEY_LEFT || key == LV_KEY_ENTER ||
                key == LV_KEY_RIGHT) {
                warning_active_ = false;
                if (LeaveSelfPage) LeaveSelfPage();
            }
            lv_event_stop_processing(event);
            return;
        }
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (!saving_ && LeaveSelfPage) LeaveSelfPage();
        } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
            if (!saving_ && cursor_ > 0) {
                alias_.erase(previous_utf8_start(alias_, cursor_),
                             cursor_ - previous_utf8_start(alias_, cursor_));
                cursor_ = previous_utf8_start(alias_, cursor_);
                cursor_visible_ = true;
                render();
            }
        } else if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
            /* Up/down have no semantic meaning in a one-line alias editor. */
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            save();
        }
        lv_event_stop_processing(event);
    }

    static void keyboard_event_cb(lv_event_t *event)
    {
        if (!event) return;
        auto *self = static_cast<LvSettingBluetoothAliasPage3 *>(
            lv_event_get_user_data(event));
        auto *item = static_cast<const key_item *>(lv_event_get_param(event));
        if (!self || !item ||
            (item->key_state != KBD_KEY_PRESSED && item->key_state != KBD_KEY_REPEATED))
            return;

        if (self->warning_active_) {
            if (item->key_code == KEY_ESC || item->key_code == KEY_ENTER ||
                item->key_code == KEY_KPENTER || item->key_code == KEY_LEFT ||
                item->key_code == KEY_RIGHT) {
                self->warning_active_ = false;
                if (self->LeaveSelfPage) self->LeaveSelfPage();
            }
            lv_event_stop_processing(event);
            return;
        }

        if (item->key_code == KEY_ESC) {
            if (!self->saving_ && self->LeaveSelfPage) self->LeaveSelfPage();
        } else if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
            self->save();
        } else if (item->key_code == KEY_BACKSPACE || item->key_code == KEY_DELETE) {
            if (!self->saving_ && self->cursor_ > 0) {
                const std::size_t start = previous_utf8_start(self->alias_, self->cursor_);
                self->alias_.erase(start, self->cursor_ - start);
                self->cursor_ = start;
                self->render();
            }
        } else if (item->key_code == KEY_LEFT) {
            self->cursor_ = previous_utf8_start(self->alias_, self->cursor_);
            self->cursor_visible_ = true;
            self->render();
        } else if (item->key_code == KEY_RIGHT) {
            self->cursor_ = next_utf8_end(self->alias_, self->cursor_);
            self->cursor_visible_ = true;
            self->render();
        } else if (item->utf8[0]) {
            self->append_text(item->utf8);
        }
        lv_event_stop_processing(event);
    }

    NodeIter parent_node_;
    std::string alias_;
    std::string backend_alias_;
    std::string alias_before_save_;
    std::size_t cursor_ = 0;
    bool saving_ = false;
    bool cursor_visible_ = true;
    bool status_known_ = false;
    bool powered_ = false;
    bool status_pending_ = false;
    bool warning_active_ = false;
    std::string error_message_;
    std::function<void(std::string)> saved_callback_;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
    lv_timer_t *api_timer_ = nullptr;
    lv_timer_t *cursor_timer_ = nullptr;
    Cp0BoundedTaskRegistry api_tasks_;
    std::shared_ptr<Cp0BluetoothUiApiDispatch> api_dispatch_ =
        std::make_shared<Cp0BluetoothUiApiDispatch>();
    std::shared_ptr<bool> lifetime_ = std::make_shared<bool>(true);
    uint64_t generation_ = 1;
};
