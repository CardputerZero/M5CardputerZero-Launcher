#pragma once

#include <algorithm>
#include <charconv>
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
#include "hal_lvgl_bsp.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "setting_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_componens.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

extern "C" {
extern const lv_image_dsc_t setting_ok;
extern const lv_image_dsc_t setting_cross;
}

struct Cp0BluetoothUiApiDispatch {
    std::mutex mutex;
    bool stopped = false;
    std::deque<std::function<void()>> pending;
};

inline void cp0_bluetooth_ui_enqueue(
    const std::shared_ptr<Cp0BluetoothUiApiDispatch> &dispatch,
    const std::weak_ptr<bool> &lifetime,
    std::function<void(int, std::string)> handler,
    int code,
    std::string data)
{
    if (!dispatch) return;
    try {
        std::lock_guard<std::mutex> lock(dispatch->mutex);
        if (dispatch->stopped || lifetime.expired()) return;
        dispatch->pending.emplace_back(
            [lifetime, handler = std::move(handler), code, data = std::move(data)]() mutable {
                if (lifetime.expired()) return;
                if (handler) handler(code, std::move(data));
            });
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
        {
            std::lock_guard<std::mutex> lock(api_dispatch_->mutex);
            api_dispatch_->stopped = true;
            api_dispatch_->pending.clear();
        }
        ++generation_;
        page_lifetime_.reset();
        stop_scan();
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
                                   uint64_t generation,
                                   int code,
                                   std::string data) noexcept
    {
        if (!dispatch) return;
        try {
            std::lock_guard<std::mutex> lock(dispatch->mutex);
            if (dispatch->stopped) return;
            dispatch->pending.push_back(
                ApiResult{owner, lifetime, handler, generation, code, std::move(data)});
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
            std::lock_guard<std::mutex> lock(self->api_dispatch_->mutex);
            pending.swap(self->api_dispatch_->pending);
        }

        for (auto &result : pending) {
            if (result.lifetime.expired() || result.owner != self ||
                result.generation != self->generation_ || !self->ComponensObj)
                continue;

            try {
                if (result.handler)
                    result.handler(result.code, std::move(result.data));
            } catch (...) {
            }
        }
        self->api_tasks_.reap_finished();
    }

    void request_api(std::list<std::string> arguments, ApiHandler handler)
    {
        const std::weak_ptr<bool> lifetime = page_lifetime_;
        const uint64_t request_generation = generation_;
        const auto dispatch = api_dispatch_;
        const ApiHandler fallback_handler = handler;
        auto callback = [dispatch,
                         owner = this,
                         lifetime,
                         request_generation,
                         handler = std::move(handler)](int code, std::string data) mutable {
            enqueue_api_result(dispatch,
                               owner,
                               lifetime,
                               handler,
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
                               request_generation,
                               -1,
                               "Bluetooth request could not be scheduled");
        }
    }

    static std::vector<std::string> split_fields(const std::string &record)
    {
        std::vector<std::string> fields;
        std::istringstream input(record);
        std::string field;
        while (std::getline(input, field, '\t')) fields.push_back(field);
        if (!record.empty() && record.back() == '\t') fields.emplace_back();
        return fields;
    }

    static bool parse_integer(std::string_view text, int &value)
    {
        if (text.empty()) return false;
        int parsed = 0;
        const char *begin = text.data();
        const char *end = begin + text.size();
        const auto result = std::from_chars(begin, end, parsed, 10);
        if (result.ec != std::errc() || result.ptr != end) return false;
        value = parsed;
        return true;
    }

    static bool parse_bool(std::string_view text, bool &value)
    {
        int parsed = 0;
        if (!parse_integer(text, parsed) || (parsed != 0 && parsed != 1)) return false;
        value = parsed != 0;
        return true;
    }

    static bool decode_status(const std::string &data,
                              bool &powered,
                              std::string &address)
    {
        const auto fields = split_fields(data);
        if (fields.size() != 4) return false;
        bool discoverable = false;
        if (!parse_bool(fields[0], powered) ||
            !parse_bool(fields[2], discoverable))
            return false;
        address = fields[1];
        return true;
    }

    static void copy_device_field(char *destination,
                                  size_t destination_size,
                                  const std::string &source)
    {
        if (!destination || destination_size == 0) return;
        const size_t count = std::min(destination_size - 1, source.size());
        std::copy_n(source.data(), count, destination);
        destination[count] = '\0';
    }

    static bool decode_device(const std::string &line, cp0_bt_device_t &device)
    {
        const auto fields = split_fields(line);
        if (fields.size() != 6) return false;

        int rssi = 0;
        bool connected = false;
        bool paired = false;
        bool trusted = false;
        if (!parse_integer(fields[1], rssi) ||
            !parse_bool(fields[2], connected) ||
            !parse_bool(fields[3], paired) ||
            !parse_bool(fields[4], trusted))
            return false;

        cp0_bt_device_t decoded{};
        copy_device_field(decoded.address, sizeof(decoded.address), fields[0]);
        copy_device_field(decoded.name, sizeof(decoded.name), fields[5]);
        decoded.rssi = rssi;
        decoded.connected = connected ? 1 : 0;
        decoded.paired = paired ? 1 : 0;
        decoded.trusted = trusted ? 1 : 0;
        device = decoded;
        return true;
    }

    static void decode_devices(const std::string &data,
                               std::vector<cp0_bt_device_t> &devices)
    {
        devices.clear();
        std::istringstream lines(data);
        std::string line;
        while (static_cast<int>(devices.size()) < CP0_BT_DEVICE_MAX &&
               std::getline(lines, line)) {
            if (line.empty()) continue;
            cp0_bt_device_t device{};
            if (decode_device(line, device)) devices.push_back(device);
        }
    }

    static std::string device_text(const char *value, size_t size)
    {
        if (!value || size == 0) return {};
        size_t length = 0;
        while (length < size && value[length]) ++length;
        return std::string(value, length);
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
        status_pending_ = true;
        request_api({"BtStatus"}, [this](int code, std::string data) {
            status_pending_ = false;
            status_known_ = code == 0 && decode_status(data, powered_, adapter_address_);
            if (!status_known_) {
                powered_ = false;
                adapter_address_.clear();
                devices_.clear();
                clamp_selection();
                error_message_ = "Bluetooth service unavailable.";
                render();
                return;
            }

            error_message_.clear();
            if (!powered_) {
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
        });
    }

    void refresh_devices()
    {
        if (!status_known_ || !powered_ || action_pending_ || list_pending_) return;
        list_pending_ = true;
        loading_ = true;
        render();

        const char *command = mode_ == LvSettingBluetoothListMode::Scan
            ? "BtList"
            : "BtConnectedList";
        request_api({command, std::to_string(CP0_BT_DEVICE_MAX)},
                    [this](int code, std::string data) {
                        list_pending_ = false;
                        loading_ = false;
                        if (code < 0) {
                            devices_.clear();
                            error_message_ = "Bluetooth device list unavailable.";
                        } else {
                            decode_devices(data, devices_);
                            error_message_.clear();
                        }
                        clamp_selection();
                        render();
                    });
    }

    void start_scan()
    {
        if (mode_ != LvSettingBluetoothListMode::Scan || !powered_ ||
            scan_start_pending_ || discovery_active_)
            return;

        scan_start_pending_ = true;
        loading_ = true;
        error_message_.clear();
        render();
        request_api({"BtDiscoveryStart"}, [this](int code, std::string) {
            scan_start_pending_ = false;
            if (code != 0) {
                discovery_active_ = false;
                error_message_ = "Bluetooth scan unavailable.";
                loading_ = false;
                render();
                return;
            }

            discovery_active_ = true;
            if (!scan_timer_)
                scan_timer_ = lv_timer_create(scan_timer_cb, 1500, this);
            refresh_devices();
        });
    }

    void restart_scan()
    {
        if (mode_ != LvSettingBluetoothListMode::Scan || !powered_ || action_pending_)
            return;

        ++generation_;
        list_pending_ = false;
        stop_scan();
        start_scan();
    }

    void stop_scan()
    {
        if (scan_timer_) {
            lv_timer_delete(scan_timer_);
            scan_timer_ = nullptr;
        }
        if (discovery_active_ || scan_start_pending_) {
            try {
                cp0_signal_bt_api({"BtDiscoveryStop"}, [](int, std::string) {});
            } catch (...) {
            }
        }
        discovery_active_ = false;
        scan_start_pending_ = false;
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

    void activate_selected()
    {
        if (action_pending_ || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(devices_.size()) || !powered_)
            return;

        const cp0_bt_device_t device = devices_[static_cast<size_t>(selected_index_)];
        const std::string address = device_text(device.address, sizeof(device.address));
        if (address.empty()) return;

        action_pending_ = true;
        action_address_ = address;
        ++generation_;
        list_pending_ = false;
        stop_scan();
        action_message_ = device.connected ? "Disconnecting..."
            : device.paired ? "Connecting..." : "Pairing...";
        render();

        if (device.connected) {
            request_action("BtDisconnect", address);
        } else if (device.paired) {
            request_action("BtConnect", address);
        } else {
            request_api({"BtPair", address}, [this](int code, std::string) {
                if (code != 0) {
                    finish_action(code);
                    return;
                }
                action_message_ = "Connecting...";
                render();
                request_action("BtConnect", action_address_);
            });
        }
    }

    void request_action(const char *command, const std::string &address)
    {
        request_api({command, address}, [this](int code, std::string) {
            finish_action(code);
        });
    }

    void finish_action(int code)
    {
        action_pending_ = false;
        action_message_.clear();
        action_address_.clear();
        if (code != 0) {
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
        if (address.empty()) return;
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
    bool scan_start_pending_ = false;
    bool discovery_active_ = false;
    std::string adapter_address_;
    std::string error_message_;
    std::string action_message_;
    std::string action_address_;
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
        render();
        request_api({"BtStatus"}, [this](int code, std::string data) {
            const auto fields = split_fields(data);
            status_known_ = code == 0 && fields.size() == 4;
            if (status_known_) {
                powered_ = fields[0] == "1";
                if (!fields[3].empty()) {
                    alias_ = fields[3];
                    cursor_ = alias_.size();
                }
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

    static std::vector<std::string> split_fields(const std::string &record)
    {
        std::vector<std::string> fields;
        std::istringstream input(record);
        std::string field;
        while (std::getline(input, field, '\t')) fields.push_back(field);
        return fields;
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

        std::string display = alias_.substr(0, cursor_);
        display.push_back('_');
        display += alias_.substr(cursor_);
        create_label(ComponensObj,
                     display.c_str(),
                     64,
                     36,
                     SCREEN_W - 72,
                     0xFFFFFF,
                     &lv_font_montserrat_14);

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
        create_label(dialog, "Bluetooth power is off", 12, 10, 250,
                     0xFFAA00, &lv_font_montserrat_14);
        create_label(dialog, "Turn on Power before continuing.", 12, 36, 250,
                     0xCCCCCC, &lv_font_montserrat_12);
        create_label(dialog, "OK", 246, 68, 28,
                     0x58A6FF, &lv_font_montserrat_12);
    }

    void request_api(std::list<std::string> arguments, ApiHandler handler)
    {
        const auto dispatch = api_dispatch_;
        const auto lifetime = lifetime_;
        const ApiHandler fallback_handler = handler;
        auto callback = [dispatch, lifetime, handler = std::move(handler)](
                            int code, std::string data) mutable {
            cp0_bluetooth_ui_enqueue(dispatch, lifetime, handler, code, std::move(data));
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
                                     -1,
                                     "Bluetooth request could not be scheduled");
    }

    static void api_result_timer_cb(lv_timer_t *timer) noexcept
    {
        auto *self = timer
            ? static_cast<LvSettingBluetoothAliasPage3 *>(lv_timer_get_user_data(timer))
            : nullptr;
        if (!self || timer != self->api_timer_) return;
        std::deque<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(self->api_dispatch_->mutex);
            pending.swap(self->api_dispatch_->pending);
        }
        for (auto &callback : pending) {
            try {
                if (callback) callback();
            } catch (...) {
            }
        }
        self->api_tasks_.reap_finished();
    }

    void append_text(const char *text)
    {
        if (saving_ || !text || !text[0]) return;
        const std::size_t length = std::strlen(text);
        if (alias_.size() + length > MAX_ALIAS_BYTES) return;
        if (std::strpbrk(text, "\t\r\n")) return;
        alias_.insert(cursor_, text, length);
        cursor_ += length;
        error_message_.clear();
        render();
    }

    void save()
    {
        if (saving_ || alias_.empty()) return;
        if (!status_known_ || !powered_) {
            show_power_warning();
            return;
        }
        saving_ = true;
        error_message_.clear();
        render();
        request_api({"BtAlias", alias_}, [this](int code, std::string) {
            saving_ = false;
            if (code != 0) {
                error_message_ = "Set alias failed.";
                render();
                return;
            }
            if (saved_callback_) saved_callback_(alias_);
            if (LeaveSelfPage) LeaveSelfPage();
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
            self->render();
        } else if (item->key_code == KEY_RIGHT) {
            self->cursor_ = next_utf8_end(self->alias_, self->cursor_);
            self->render();
        } else if (item->utf8[0]) {
            self->append_text(item->utf8);
        }
        lv_event_stop_processing(event);
    }

    NodeIter parent_node_;
    std::string alias_;
    std::size_t cursor_ = 0;
    bool saving_ = false;
    bool status_known_ = false;
    bool powered_ = false;
    bool warning_active_ = false;
    std::string error_message_;
    std::function<void(std::string)> saved_callback_;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_event_dsc_ = nullptr;
    lv_timer_t *api_timer_ = nullptr;
    Cp0BoundedTaskRegistry api_tasks_;
    std::shared_ptr<Cp0BluetoothUiApiDispatch> api_dispatch_ =
        std::make_shared<Cp0BluetoothUiApiDispatch>();
    std::shared_ptr<bool> lifetime_ = std::make_shared<bool>(true);
};
