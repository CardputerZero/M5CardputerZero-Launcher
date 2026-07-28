from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "main/ui/page_app/setting/bluetooth.hpp").read_text(encoding="utf-8")
CONTROLLER = (ROOT / "main/ui/page_app/setting/bluetooth.cpp").read_text(encoding="utf-8")
SERVICE = (ROOT / "main/ui/page_app/setting/bluetooth_service.cpp").read_text(encoding="utf-8")
INPUT = (ROOT / "main/ui/page_app/ui_app_setup_input.cpp").read_text(encoding="utf-8")


def body_between(source: str, start_signature: str, next_signature: str) -> str:
    start = source.index(start_signature)
    end = source.index(next_signature, start)
    return source[start:end]


def test_power_dependent_entries_use_shared_guard():
    entries = (
        (CONTROLLER, "void Bluetooth::enter_devices", "void Bluetooth::enter_alias"),
        (CONTROLLER, "void Bluetooth::enter_alias", "void Bluetooth::handle_alias_key"),
        (CONTROLLER, "void Bluetooth::enter_scan", "bool Bluetooth::require_power_enabled"),
        (SERVICE, "void Bluetooth::toggle_discoverable", "void Bluetooth::start_scan_timer"),
        (SERVICE, "void Bluetooth::start_scan_timer", "void Bluetooth::scan_timer_cb"),
        (SERVICE, "void Bluetooth::resume_scan_discovery", "void Bluetooth::stop_scan_timer"),
    )
    for source, start, end in entries:
        assert "require_power_enabled(" in body_between(source, start, end)


def test_disabled_power_shows_warning_and_returns_to_power():
    guard = body_between(
        CONTROLLER,
        "bool Bluetooth::require_power_enabled",
        "void Bluetooth::handle_power_warning_key",
    )
    assert "get_status().powered != 0" in guard
    assert "SetupViewState::BT_POWER_WARNING" in guard
    assert "show_power_warning(page)" in guard

    handler = body_between(
        CONTROLLER,
        "void Bluetooth::handle_power_warning_key",
        "void Bluetooth::rebuild_rows",
    )
    assert "access.set_view(SetupViewState::SUB)" in handler
    assert "access.select_sub(0, 6)" in handler
    assert "case ViewState::BT_POWER_WARNING:" in INPUT
    assert "bluetooth_.handle_power_warning_key(*this, key)" in INPUT
    assert "require_power_enabled" in HEADER

    watchdog = body_between(
        SERVICE,
        "void Bluetooth::scan_timer_cb",
        "void Bluetooth::suspend_scan_discovery",
    )
    resume = watchdog.index("resume_scan_discovery()")
    state_check = watchdog.index("is_view(SetupViewState::BT_LIST)", resume)
    overwrite = watchdog.index("show_action", resume)
    assert resume < state_check < overwrite


if __name__ == "__main__":
    test_power_dependent_entries_use_shared_guard()
    test_disabled_power_shows_warning_and_returns_to_power()
