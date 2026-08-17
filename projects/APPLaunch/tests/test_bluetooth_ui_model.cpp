#include "../main/ui/model/bluetooth_ui_model.hpp"

#include <cassert>
#include <string>

int main()
{
    // Status snapshot decode.
    BluetoothStatusSnapshot status;
    status.named_only = false;
    assert(BluetoothStatusSnapshot::decode(
        "1\tAA:BB:CC:DD:EE:FF\t0\tCardputerZero", status));
    assert(status.powered);
    assert(status.address == "AA:BB:CC:DD:EE:FF");
    assert(!status.discoverable);
    assert(status.alias == "CardputerZero");
    assert(!status.named_only); // caller's flag preserved across decode
    const BluetoothStatusSnapshot unchanged = status;
    assert(!BluetoothStatusSnapshot::decode("yes\tAA\t0\tx", status));
    assert(status.alias == unchanged.alias);

    // List snapshot decode preserves rssi/trusted and drops malformed lines.
    BluetoothListSnapshot list = BluetoothListSnapshot::decode(
        "AA:BB:CC:DD:EE:01\t-67\t1\t0\t1\tKeyboard\n"
        "garbage-line\n"
        "AA:BB:CC:DD:EE:02\t-30\t0\t1\t0\tSpeaker\n");
    assert(list.devices.size() == 2);
    assert(list.devices[0].address == "AA:BB:CC:DD:EE:01");
    assert(list.devices[0].rssi == -67);
    assert(list.devices[0].connected);
    assert(list.devices[0].trusted);
    assert(list.devices[1].address == "AA:BB:CC:DD:EE:02");
    assert(list.devices[1].paired);
    assert(!list.empty());
    assert(BluetoothListSnapshot::decode("").empty());

    BluetoothUiModel model;
    assert(model.sub_page() == BluetoothSubPage::NONE);
    assert(model.named_only());
    assert(model.alias() == BluetoothPageModel::DEFAULT_ALIAS);

    model.set_named_only(false);
    assert(!model.named_only());

    model.apply_status(status);
    assert(model.status().powered);
    assert(model.discoverable() == status.discoverable);
    assert(model.alias() == "CardputerZero");

    model.set_sub_page(BluetoothSubPage::CONNECTED);
    assert(model.sub_page() == BluetoothSubPage::CONNECTED);
    assert(model.list_mode() == BluetoothListMode::MANAGED);
    model.apply_list(list);
    // managed mode keeps only connected devices
    assert(!model.rows().empty());
    assert(model.selected_device_index() == 0);

    model.set_sub_page(BluetoothSubPage::SCAN);
    assert(model.list_mode() == BluetoothListMode::SCAN);
    model.apply_list(list);
    assert(model.selected_device_index() >= 0);

    model.set_sub_page(BluetoothSubPage::NONE);
    assert(model.sub_page() == BluetoothSubPage::NONE);
    assert(model.list().empty());

    // alias edit flow.
    model.set_alias("");
    model.begin_alias_edit();
    assert(model.alias_input() == BluetoothPageModel::DEFAULT_ALIAS);
    while (model.erase_alias_character()) {}
    assert(model.append_alias_text("MyDevice"));
    assert(model.sanitized_alias() == "MyDevice");
    model.set_alias("MyDevice");
    assert(model.alias() == "MyDevice");

    // named_only toggle re-filters the current list snapshot (regression:
    // changing the filter must re-derive rows, not render a stale list).
    {
        BluetoothListSnapshot mixed;
        mixed.devices = {
            {"AA:BB:CC:DD:EE:01", "Keyboard", false, false, -67, false},
            {"AA:BB:CC:DD:EE:02", "", false, false, -30, false},
        };
        BluetoothUiModel filtered_model;
        filtered_model.set_sub_page(BluetoothSubPage::SCAN);
        filtered_model.set_named_only(true);
        filtered_model.apply_list(mixed);
        const size_t named_only_rows = filtered_model.rows().size();
        filtered_model.set_named_only(false);
        assert(filtered_model.rows().size() > named_only_rows);
    }

    // Initial load state machine transitions.
    BluetoothUiModel load_model;
    assert(load_model.load_state() == BluetoothSessionLoadState::CREATED);
    assert(!load_model.is_initial_load_finished());
    load_model.begin_load();
    assert(load_model.load_state() == BluetoothSessionLoadState::LOADING);
    load_model.mark_ready();
    assert(load_model.load_state() == BluetoothSessionLoadState::READY);
    assert(load_model.is_initial_load_finished());

    BluetoothUiModel failed_model;
    failed_model.begin_load();
    failed_model.mark_failed();
    assert(failed_model.load_state() == BluetoothSessionLoadState::FAILED);
    assert(failed_model.is_initial_load_finished());

    BluetoothUiModel stopped_model;
    stopped_model.mark_stopped();
    assert(stopped_model.load_state() == BluetoothSessionLoadState::STOPPED);

    return 0;
}
