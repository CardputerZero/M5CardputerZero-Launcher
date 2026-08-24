#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
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

enum class LvSettingBluetoothListMode { Connected, Scan };

class LvSettingBluetoothPage3 : public DComponens::LvglComponensBase {
public:
    static constexpr int SCREEN_W     = 320;
    static constexpr int SCREEN_H     = 150;
    static constexpr int ROW_H        = 20;
    static constexpr int ROW_Y        = 22;
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
        lv_obj_clean(ComponensObj);

        std::string title = mode_ == LvSettingBluetoothListMode::Scan
            ? "Bluetooth Scan"
            : "Bluetooth Connected";
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
            SCREEN_W - 16,
            0x58A6FF,
            cp0_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD));

        const char *hint = mode_ == LvSettingBluetoothListMode::Scan
            ? "OK:act  Del:restart  ESC:back"
            : "OK:toggle  Del:remove  ESC:back";
        if (action_pending_) {
            create_label(ComponensObj,
                         action_message_.c_str(),
                         8,
                         58,
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
                             52,
                             SCREEN_W - 16,
                             error_message_.empty() ? 0x666666 : 0xFFAA00,
                             &lv_font_montserrat_12);
            }

            const int count = static_cast<int>(devices_.size());
            const int offset = count <= VISIBLE_ROWS
                ? 0
                : std::clamp(selected_index_ - VISIBLE_ROWS / 2,
                             0,
                             count - VISIBLE_ROWS);
            for (int visible = 0;
                 visible < VISIBLE_ROWS && offset + visible < count;
                 ++visible) {
                const int index = offset + visible;
                const auto &device = devices_[static_cast<size_t>(index)];
                const int y = ROW_Y + visible * ROW_H;
                const bool selected = index == selected_index_;
                if (selected) {
                    lv_obj_t *background = lv_obj_create(ComponensObj);
                    if (background) {
                        lv_obj_set_size(background, SCREEN_W - 8, ROW_H - 1);
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
                             160,
                             color,
                             &lv_font_montserrat_12);
                create_label(ComponensObj,
                             address.c_str(),
                             8,
                             y + 11,
                             200,
                             selected ? 0xBBBBBB : 0x777777,
                             &lv_font_montserrat_10);

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
                    226,
                    y + 4,
                    86,
                    color,
                    &lv_font_montserrat_10);
                if (state_label)
                    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            }
        }

        create_label(ComponensObj,
                     hint,
                     8,
                     HINT_Y,
                     SCREEN_W - 16,
                     0x555555,
                     &lv_font_montserrat_10);
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
    Cp0BoundedTaskRegistry api_tasks_;
    std::shared_ptr<ApiDispatchState> api_dispatch_ = std::make_shared<ApiDispatchState>();
    std::shared_ptr<bool> page_lifetime_ = std::make_shared<bool>(true);
    uint64_t generation_ = 1;
};

class LvSettingBluetoothConnectedPage3 : public LvSettingBluetoothPage3 {
public:
    LvSettingBluetoothConnectedPage3() = default;

    LvSettingBluetoothConnectedPage3(lv_obj_t *parent,
                                     const NodeIter &parent_node,
                                     std::function<void()> back_callback)
        : LvSettingBluetoothPage3(parent,
                                   parent_node,
                                   std::move(back_callback),
                                   LvSettingBluetoothListMode::Connected)
    {
    }
};

class LvSettingBluetoothScanPage3 : public LvSettingBluetoothPage3 {
public:
    LvSettingBluetoothScanPage3() = default;

    LvSettingBluetoothScanPage3(lv_obj_t *parent,
                                const NodeIter &parent_node,
                                std::function<void()> back_callback)
        : LvSettingBluetoothPage3(parent,
                                   parent_node,
                                   std::move(back_callback),
                                   LvSettingBluetoothListMode::Scan)
    {
    }
};
