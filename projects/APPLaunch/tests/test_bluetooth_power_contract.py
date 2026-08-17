from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "main/ui/page_app/setting/bluetooth_ui_session.hpp").read_text(encoding="utf-8")
CONTROLLER = (ROOT / "main/ui/page_app/setting/bluetooth_ui_session.cpp").read_text(encoding="utf-8")
VIEW = (ROOT / "main/ui/page_app/setting/bluetooth_ui_session_view.cpp").read_text(encoding="utf-8")
INPUT = (ROOT / "main/ui/page_app/ui_app_setup_input.cpp").read_text(encoding="utf-8")


def body_between(source: str, start_signature: str, next_signature: str) -> str:
    start = source.index(start_signature)
    end = source.index(next_signature, start)
    return source[start:end]


def test_power_dependent_entries_use_shared_guard():
    # Every power-dependent entry point funnels through require_power_enabled().
    entries = (
        (CONTROLLER, "void BluetoothUiSession::enter_devices",
         "void BluetoothUiSession::enter_connected"),
        (CONTROLLER, "void BluetoothUiSession::enter_scan",
         "void BluetoothUiSession::enter_scan_sub"),
        (CONTROLLER, "void BluetoothUiSession::enter_alias",
         "void BluetoothUiSession::handle_alias_key"),
        (CONTROLLER, "void BluetoothUiSession::toggle_discoverable",
         "void BluetoothUiSession::activate_selected"),
        (CONTROLLER, "void BluetoothUiSession::activate_selected",
         "void BluetoothUiSession::finish_device_action"),
        (CONTROLLER, "void BluetoothUiSession::remove_selected",
         "void BluetoothUiSession::handle_list_key"),
    )
    for source, start, end in entries:
        assert "require_power_enabled(" in body_between(source, start, end)


def test_disabled_power_shows_warning_and_returns_to_power():
    guard = body_between(
        CONTROLLER,
        "void BluetoothUiSession::require_power_enabled",
        "void BluetoothUiSession::toggle_power",
    )
    assert "SetupViewState::BT_POWER_WARNING" in guard
    assert "show_power_warning(page)" in guard

    handler = body_between(
        CONTROLLER,
        "void BluetoothUiSession::handle_power_warning_key",
        "void BluetoothUiSession::require_power_enabled",
    )
    assert "access.set_view(SetupViewState::SUB)" in handler
    assert "access.select_sub(0, 6)" in handler
    assert "case ViewState::BT_POWER_WARNING:" in INPUT
    assert "bluetooth_ui_->handle_power_warning_key(*this, key)" in INPUT
    assert "require_power_enabled" in HEADER


def test_session_protocol_and_weak_delivery():
    # The backend is driven exclusively through session-scoped commands.
    for command in (
        "BtSessionInit", "BtSessionDeinit", "BtStatusGet",
        "BtConnectedListInit", "BtConnectedListGet", "BtConnectedListDeinit",
        "BtScanOn", "BtScanOff",
    ):
        assert command in CONTROLLER
    # Worker-thread completions are marshalled through a weak_ptr, never a
    # naked UISetupPage* captured by the backend.
    assert "weak_from_this()" in CONTROLLER
    assert "post_to_ui" in CONTROLLER


def test_no_synchronous_api_int():
    # The old synchronous helper that returned a default value immediately must
    # be gone from the Bluetooth UI.
    assert "api_int" not in CONTROLLER
    assert "api_int" not in VIEW


if __name__ == "__main__":
    test_power_dependent_entries_use_shared_guard()
    test_disabled_power_shows_warning_and_returns_to_power()
    test_session_protocol_and_weak_delivery()
    test_no_synchronous_api_int()
