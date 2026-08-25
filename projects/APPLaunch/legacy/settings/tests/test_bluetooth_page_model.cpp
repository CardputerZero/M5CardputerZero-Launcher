#include "../main/ui/model/bluetooth_page_model.hpp"
#include "../main/ui/model/bluetooth_scan_lifecycle_contract.hpp"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    BluetoothWireStatus wire_status;
    assert(BluetoothPageModel::decode_status_record(
        "1\tAA:BB:CC:DD:EE:FF\t0\tCardputerZero", wire_status));
    assert(wire_status.powered);
    assert(!wire_status.discoverable);
    assert(wire_status.alias == "CardputerZero");
    const BluetoothWireStatus unchanged_status = wire_status;
    assert(!BluetoothPageModel::decode_status_record(
        "yes\tAA:BB:CC:DD:EE:FF\t0\tBroken", wire_status));
    assert(wire_status.alias == unchanged_status.alias);
    assert(!BluetoothPageModel::decode_status_record("1\t\t0\tAlias", wire_status));

    BluetoothWireDevice wire_device;
    assert(BluetoothPageModel::decode_device_record(
        "AA:BB:CC:DD:EE:01\t-67\t1\t0\t1\tKeyboard", wire_device));
    assert(wire_device.rssi == -67);
    assert(wire_device.connected);
    assert(!wire_device.paired);
    assert(wire_device.trusted);
    assert(wire_device.name == "Keyboard");
    const BluetoothWireDevice unchanged_device = wire_device;
    assert(!BluetoothPageModel::decode_device_record(
        "AA:BB:CC:DD:EE:01\t-67dBm\t1\t0\t1\tKeyboard", wire_device));
    assert(wire_device.rssi == unchanged_device.rssi);
    assert(!BluetoothPageModel::decode_device_record(
        "AA:BB:CC:DD:EE:01\t-67\t2\t0\t1\tKeyboard", wire_device));
    assert(!BluetoothPageModel::decode_device_record(
        "AA:BB:CC:DD:EE:01\t-67\t1\t0\tKeyboard", wire_device));

    int timer = 0;
    int stale_timer = 0;
    int page = 0;
    int screen = 0;
    int child = 0;
    static_assert(noexcept(bluetooth_scan_callback_allowed(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        static_cast<int *>(nullptr), false)));
    assert(bluetooth_scan_callback_allowed(&timer, &timer, &page, true));
    assert(!bluetooth_scan_callback_allowed(&stale_timer, &timer, &page, true));
    assert(!bluetooth_scan_callback_allowed(&timer, &timer, nullptr, true));
    assert(!bluetooth_scan_callback_allowed(&timer, &timer, &page, false));
    assert(bluetooth_scan_callback_ready(&timer, &timer, &page));
    assert(!bluetooth_scan_callback_ready(&stale_timer, &timer, &page));
    assert(!bluetooth_scan_callback_ready(nullptr, &timer, &page));
    assert(!bluetooth_scan_callback_ready(&timer, &timer, nullptr));
    assert(bluetooth_feedback_callback_ready(&timer, &timer, &page));
    assert(!bluetooth_feedback_callback_ready(&stale_timer, &timer, &page));
    assert(!bluetooth_feedback_callback_ready(&timer, &timer, nullptr));
    static_assert(noexcept(bluetooth_feedback_callback_allowed(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        static_cast<int *>(nullptr), false)));
    assert(bluetooth_feedback_callback_allowed(&timer, &timer, &page, true));
    assert(!bluetooth_feedback_callback_allowed(&stale_timer, &timer, &page, true));
    assert(!bluetooth_feedback_callback_allowed(&timer, &timer, nullptr, true));
    assert(!bluetooth_feedback_callback_allowed(&timer, &timer, &page, false));
    assert(bluetooth_screen_delete_is_direct(&screen, &screen));
    assert(!bluetooth_screen_delete_is_direct(&child, &screen));
    assert(!bluetooth_screen_delete_is_direct(static_cast<int *>(nullptr), &screen));
    int stale_screen = 0;
    static_assert(noexcept(bluetooth_feedback_screen_delete_matches(
        static_cast<int *>(nullptr), static_cast<int *>(nullptr),
        static_cast<int *>(nullptr))));
    assert(bluetooth_feedback_screen_delete_matches(&screen, &screen, &screen));
    assert(!bluetooth_feedback_screen_delete_matches(&child, &screen, &screen));
    assert(!bluetooth_feedback_screen_delete_matches(
        &stale_screen, &stale_screen, &screen));
    assert(!bluetooth_feedback_screen_delete_matches(
        &screen, &screen, static_cast<int *>(nullptr)));
    assert(bluetooth_scan_action_needs_watchdog_recovery(true, false));
    assert(!bluetooth_scan_action_needs_watchdog_recovery(true, true));
    assert(!bluetooth_scan_action_needs_watchdog_recovery(false, false));
    static_assert(noexcept(bluetooth_action_entry_allowed(false, false)));
    assert(bluetooth_action_entry_allowed(true, false));
    assert(!bluetooth_action_entry_allowed(true, true));
    assert(bluetooth_action_entry_allowed(false, false));
    assert(bluetooth_action_entry_allowed(false, true));
    static_assert(noexcept(bluetooth_async_completion_allowed(
        false, static_cast<int *>(nullptr))));
    assert(bluetooth_async_completion_allowed(true, &page));
    assert(!bluetooth_async_completion_allowed(false, &page));
    assert(!bluetooth_async_completion_allowed(
        true, static_cast<int *>(nullptr)));
    assert(bluetooth_scan_action_should_resume(true, -1));
    assert(!bluetooth_scan_action_should_resume(true, 0));
    assert(!bluetooth_scan_action_should_resume(false, -1));

    BluetoothPageModel model;
    assert(model.named_only());
    assert(model.alias() == BluetoothPageModel::DEFAULT_ALIAS);

    model.set_alias("");
    model.begin_alias_edit();
    assert(model.alias_input() == BluetoothPageModel::DEFAULT_ALIAS);
    assert(model.erase_alias_character());
    assert(model.append_alias_text("X\nY\tZ"));
    assert(model.alias_input().find('\n') == std::string::npos);
    assert(model.alias_input().find('\t') == std::string::npos);

    model.set_alias("A");
    model.begin_alias_edit();
    const std::string long_text(BluetoothPageModel::ALIAS_INPUT_LIMIT + 10, 'x');
    assert(model.append_alias_text(long_text.c_str()));
    assert(model.alias_input().size() == BluetoothPageModel::ALIAS_INPUT_LIMIT);
    assert(!model.append_alias_text("y"));

    model.set_alias("");
    model.begin_alias_edit();
    while (model.erase_alias_character()) {}
    assert(model.sanitized_alias() == BluetoothPageModel::DEFAULT_ALIAS);

    const std::vector<BluetoothDeviceState> devices = {
        {"AA:BB:CC:DD:EE:01", "Speaker", true, true},
        {"AA:BB:CC:DD:EE:02", "", false, false},
        {"AA:BB:CC:DD:EE:03", "AA-BB-CC-DD-EE-03", false, false},
        {"AA:BB:CC:DD:EE:04", "Keyboard", true, false},
    };

    model.set_list_mode(BluetoothListMode::MANAGED);
    model.rebuild_rows(devices);
    assert(model.rows().size() == 2);
    assert(model.rows().front().is_header);
    assert(model.selected_device_index() == 0);
    model.select_next_device(-1);
    assert(model.selected_device_index() == 0);

    model.set_list_mode(BluetoothListMode::SCAN);
    model.rebuild_rows(devices);
    assert(model.rows().size() == 3);
    assert(model.selected_device_index() == 0);
    model.select_next_device(1);
    assert(model.selected_device_index() == 3);
    model.select_next_device(1);
    assert(model.selected_device_index() == 3);

    model.set_named_only(false);
    model.rebuild_rows(devices);
    assert(model.rows().size() == devices.size() + 1);
    assert(model.selected_device_index() == 1);
    model.select_next_device(1);
    assert(model.selected_device_index() == 2);

    model.set_discoverable(true);
    assert(model.discoverable());

    assert(!model.feedback_pending());
    assert(model.begin_feedback());
    assert(model.feedback_pending());
    assert(!model.begin_feedback());
    assert(model.finish_feedback());
    assert(!model.feedback_pending());
    assert(!model.finish_feedback());
    assert(model.begin_feedback());
    model.set_list_mode(BluetoothListMode::MANAGED);
    assert(!model.feedback_pending());
    model.cancel_feedback();
}
