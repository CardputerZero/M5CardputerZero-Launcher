#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

#include <memory>
#include <new>

namespace setting {

void Bluetooth::copy_string(char *destination, size_t destination_size, const std::string &source)
{
    if (!destination || destination_size == 0)
        return;
    std::snprintf(destination, destination_size, "%s", source.c_str());
}

bool Bluetooth::decode_status(const std::string &data, cp0_bt_status_t &status)
{
    BluetoothWireStatus decoded;
    if (!BluetoothPageModel::decode_status_record(data, decoded)) return false;
    cp0_bt_status_t parsed{};
    parsed.powered = decoded.powered ? 1 : 0;
    copy_string(parsed.address, sizeof(parsed.address), decoded.address);
    parsed.discoverable = decoded.discoverable ? 1 : 0;
    copy_string(parsed.alias, sizeof(parsed.alias), decoded.alias);
    status = parsed;
    return true;
}

int Bluetooth::decode_devices(const std::string &data, cp0_bt_device_t *out, int max_devices)
{
    if (!out || max_devices <= 0)
        return 0;
    int count = 0;
    std::istringstream lines(data);
    std::string line;
    while (count < max_devices && std::getline(lines, line)) {
        if (line.empty())
            continue;
        BluetoothWireDevice decoded;
        if (!BluetoothPageModel::decode_device_record(line, decoded)) continue;
        cp0_bt_device_t device{};
        copy_string(device.address, sizeof(device.address), decoded.address);
        device.rssi = decoded.rssi;
        device.connected = decoded.connected ? 1 : 0;
        device.paired = decoded.paired ? 1 : 0;
        device.trusted = decoded.trusted ? 1 : 0;
        copy_string(device.name, sizeof(device.name), decoded.name);
        out[count++] = device;
    }
    return count;
}

int Bluetooth::api_int(std::list<std::string> args, int default_value)
{
    int result = default_value;
    cp0_signal_bt_api(std::move(args), [&](int code, std::string) { result = code; });
    return result;
}

cp0_bt_status_t Bluetooth::get_status()
{
    cp0_bt_status_t status{};
    cp0_signal_bt_api({"BtStatus"}, [&](int code, std::string data) {
        if (code == 0)
            decode_status(data, status);
    });
    return status;
}

int Bluetooth::set_power(int enabled)
{
    return api_int({"BtPower", std::to_string(enabled)});
}

int Bluetooth::rfkill_blocked()
{
    const char *argv[] = {
        "/usr/sbin/rfkill", "--noheadings", "--output", "TYPE,SOFT", nullptr
    };
    char output[512] = {};
    if (cp0_process_capture_argv(argv, output, sizeof(output)) != 0)
        return -1;

    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string type;
        std::string soft;
        fields >> type >> soft;
        if (type == "bluetooth")
            return soft == "blocked" ? 1 : 0;
    }
    return -1;
}

int Bluetooth::set_alias(const std::string &alias)
{
    return api_int({"BtAlias", alias});
}

int Bluetooth::set_discoverable(int enabled)
{
    return api_int({"BtDiscoverable", std::to_string(enabled)});
}

int Bluetooth::device_command(const char *command, const char *address)
{
    return api_int({command ? std::string(command) : std::string(),
                    address ? std::string(address) : std::string()});
}

int Bluetooth::device_list(const char *command, cp0_bt_device_t *out, int max_devices)
{
    int count = 0;
    cp0_signal_bt_api({command ? std::string(command) : std::string(), std::to_string(max_devices)},
                      [&](int code, std::string data) {
                          count = out && max_devices > 0 ? decode_devices(data, out, max_devices) : code;
                      });
    return count;
}

void Bluetooth::refresh_status(UISetupPage &page)
{
    cp0_bt_status_t status = get_status();
    for (auto &menu : SetupPageAccess(page).menus()) {
        if (menu.label != "Bluetooth") continue;
        menu.sub_items[0].toggle_state = status.powered != 0;
        model_.set_discoverable(status.discoverable != 0);
        model_.set_alias(status.alias);
        menu.sub_items[1].label = "Alias: " + model_.alias();
        menu.sub_items[2].toggle_state = model_.discoverable();
        break;
    }
}

void Bluetooth::toggle_power(UISetupPage &page)
{
    for (auto &menu : SetupPageAccess(page).menus()) {
        if (menu.label != "Bluetooth") continue;
        bool enabled = menu.sub_items[0].toggle_state;
        if (!enabled) {
            stop_scan_timer();
            set_power(0);
            refresh_status(page);
            break;
        }

        if (sudo_operation_.active()) {
            refresh_status(page);
            break;
        }

        int blocked = rfkill_blocked();
        if (blocked <= 0) {
            if (blocked < 0)
                std::fprintf(stderr, "Bluetooth: unable to query /usr/sbin/rfkill; trying BlueZ power on\n");
            set_power(1);
            refresh_status(page);
            break;
        }

        struct Context {
            UISetupPage *page;
            AsyncOperationLifecycle::Token token;
        };
        AsyncOperationLifecycle::Token token = sudo_operation_.begin();
        auto *context = new (std::nothrow) Context{&page, token};
        if (!context) {
            sudo_operation_.abort(token);
            refresh_status(page);
            break;
        }

        uint64_t request_id = 0;
        int result = -1;
        cp0_signal_sudo_argv_async({"/usr/sbin/rfkill", "unblock", "bluetooth"}, 60000, 30000,
            [context](int result_code, int exit_code) {
                std::unique_ptr<Context> owned(context);
                if (!context->token.complete()) return;
                if (result_code == CP0_SUDO_RESULT_SUCCESS && exit_code == 0) {
                    if (Bluetooth::set_power(1) != 0)
                        std::fprintf(stderr, "Bluetooth: BlueZ power on failed after rfkill unblock\n");
                } else {
                    std::fprintf(stderr, "Bluetooth: sudo /usr/sbin/rfkill unblock bluetooth failed\n");
                }
                struct Completion {
                    UISetupPage *page;
                    AsyncOperationLifecycle::Token token;
                };
                auto *completion = new (std::nothrow) Completion{context->page, context->token};
                if (!completion)
                    return;
                if (lv_async_call([](void *user) noexcept {
                    std::unique_ptr<Completion> completion(static_cast<Completion *>(user));
                    try {
                        if (!bluetooth_async_completion_allowed(
                                completion->token.current(), completion->page)) return;
                        SetupPageAccess access(*completion->page);
                        access.bluetooth().refresh_status(*completion->page);
                        if (access.is_view(SetupViewState::SUB))
                            access.build_sub_view();
                    } catch (...) {
                    }
                }, completion) != LV_RESULT_OK) {
                    delete completion;
                }
            }, [&](int code, uint64_t id) { result = code; request_id = id; });
        if (result != 0) {
            delete context;
            sudo_operation_.abort(token);
            refresh_status(page);
        } else {
            sudo_operation_.activate(token, request_id);
        }
        break;
    }
}

void Bluetooth::toggle_named_only(UISetupPage &page)
{
    SetupPageAccess access(page);
    for (auto &menu : access.menus()) {
        if (menu.label != "Bluetooth") continue;
        if (menu.sub_items.size() < 4) break;
        const bool previous = access.config_get_int("bt_named_only", 1) != 0;
        const bool desired = menu.sub_items[3].toggle_state;
        if (!access.config_set_int("bt_named_only", desired ? 1 : 0) ||
            !access.config_save()) {
            access.config_set_int("bt_named_only", previous ? 1 : 0);
            access.config_save();
            menu.sub_items[3].toggle_state = previous;
            model_.set_named_only(previous);
        } else {
            model_.set_named_only(desired);
        }
        break;
    }
    if (access.is_view(SetupViewState::BT_LIST))
        build_list(page);
}

void Bluetooth::toggle_discoverable(UISetupPage &page)
{
    if (!require_power_enabled(page)) return;
    for (auto &menu : SetupPageAccess(page).menus()) {
        if (menu.label != "Bluetooth") continue;
        model_.set_discoverable(menu.sub_items[2].toggle_state);
        if (set_discoverable(model_.discoverable() ? 1 : 0) != 0) {
            model_.set_discoverable(!model_.discoverable());
            menu.sub_items[2].toggle_state = model_.discoverable();
        }
        break;
    }
}

void Bluetooth::start_scan_timer(UISetupPage &page)
{
    if (!require_power_enabled(page)) return;
    stop_scan_timer();
    discovery_active_ = api_int({"BtDiscoveryStart"}, 0) == 0;
    refresh_devices();
    build_list(page);
    if (!discovery_active_)
        return;
    scan_page_ = &page;
    scan_callback_enabled_ = true;
    scan_timer_ = lv_timer_create(scan_timer_cb, 2500, this);
    if (!scan_timer_) {
        scan_callback_enabled_ = false;
        scan_page_ = nullptr;
        api_int({"BtDiscoveryStop"}, 0);
        discovery_active_ = false;
    }
}

void Bluetooth::scan_timer_cb(lv_timer_t *timer) noexcept
{
    auto *bluetooth = static_cast<Bluetooth *>(lv_timer_get_user_data(timer));
    if (!bluetooth || !bluetooth_scan_callback_allowed(
            timer, bluetooth->scan_timer_, bluetooth->scan_page_,
            bluetooth->scan_callback_enabled_))
        return;
    try {
        UISetupPage *page = bluetooth->scan_page_;
        SetupPageAccess access(*page);
        if (!access.is_view(SetupViewState::BT_LIST) ||
            bluetooth->model_.list_mode() != BluetoothListMode::SCAN)
            return;
        if (bluetooth->action_busy_) {
            if (!bluetooth_scan_action_needs_watchdog_recovery(
                    bluetooth->action_busy_, bluetooth->action_operation_.active()))
                return;
            bluetooth->action_busy_ = false;
            bluetooth->resume_scan_discovery();
            if (!access.is_view(SetupViewState::BT_LIST)) return;
            bluetooth->show_action(*page, "Bluetooth action failed", 0xFF4444);
        }
        bluetooth->refresh_devices();
        bluetooth->build_list(*page);
    } catch (...) {
        bluetooth->scan_callback_enabled_ = false;
    }
}

void Bluetooth::suspend_scan_discovery()
{
    if (!discovery_active_)
        return;
    api_int({"BtDiscoveryStop"}, 0);
    discovery_active_ = false;
}

void Bluetooth::resume_scan_discovery()
{
    if (discovery_active_ || !scan_timer_ || !scan_page_)
        return;
    if (!require_power_enabled(*scan_page_)) return;
    discovery_active_ = api_int({"BtDiscoveryStart"}, 0) == 0;
}

void Bluetooth::stop_scan_timer()
{
    scan_callback_enabled_ = false;
    if (scan_timer_) {
        lv_timer_delete(scan_timer_);
        scan_timer_ = nullptr;
    }
    scan_page_ = nullptr;
    suspend_scan_discovery();
}

void Bluetooth::refresh_devices()
{
    if (model_.list_mode() == BluetoothListMode::MANAGED)
        device_count_ = device_list("BtConnectedList", devices_, CP0_BT_DEVICE_MAX);
    else
        device_count_ = device_list("BtList", devices_, CP0_BT_DEVICE_MAX);
    if (device_count_ < 0)
        device_count_ = 0;
    if (device_count_ == 0)
        model_.clear_selection();
}

void Bluetooth::do_scan(UISetupPage &page)
{
    enter_scan(page);
}

} // namespace setting
