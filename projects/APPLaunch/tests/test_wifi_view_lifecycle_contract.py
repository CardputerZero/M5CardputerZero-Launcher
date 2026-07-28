from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "main/ui/page_app/setting/wifi.hpp").read_text(encoding="utf-8")
SOURCE = (ROOT / "main/ui/page_app/setting/wifi_view.cpp").read_text(encoding="utf-8")
CONTROLLER = (ROOT / "main/ui/page_app/setting/wifi.cpp").read_text(encoding="utf-8")
INPUT = (ROOT / "main/ui/page_app/ui_app_setup_input.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    match = re.search(re.escape(signature) + r"\s*\(\)\s*\{([^}]*)\}", source, re.DOTALL)
    assert match, f"missing function body: {signature}"
    return match.group(1)


def test_owned_wifi_views_unmount_before_member_destruction():
    for view in ("WiFiListView", "WiFiPasswordView", "WiFiSsidView"):
        assert f"~{view}();" in HEADER
        assert "unmount();" in function_body(SOURCE, f"{view}::~{view}")


def test_wifi_controller_unmounts_list_while_view_is_alive():
    body = function_body(CONTROLLER, "WiFi::~WiFi")
    assert "list_view_.unmount();" in body
    assert body.index("list_view_.unmount();") < body.index("clear_password_view();")


def test_scan_respects_user_disabled_radio():
    hidden_start = CONTROLLER.index("void WiFi::enter_hidden_wifi(UISetupPage &page)")
    hidden_end = CONTROLLER.index("void WiFi::enter_scan", hidden_start)
    hidden_body = CONTROLLER[hidden_start:hidden_end]
    assert "if (!require_radio_enabled(page)) return;" in hidden_body

    enter_start = CONTROLLER.index("void WiFi::enter_scan(UISetupPage &page)")
    enter_end = CONTROLLER.index("bool WiFi::require_radio_enabled", enter_start)
    enter_body = CONTROLLER[enter_start:enter_end]
    assert "if (!require_radio_enabled(page)) return;" in enter_body
    assert enter_body.index("require_radio_enabled(page)") < enter_body.index("start_scan(page)")

    guard_start = CONTROLLER.index("bool WiFi::require_radio_enabled")
    guard_end = CONTROLLER.index("void WiFi::handle_power_warning_key", guard_start)
    guard_body = CONTROLLER[guard_start:guard_end]
    assert "cp0_wifi_radio_enabled() != 0" in guard_body
    assert "SetupViewState::WIFI_POWER_WARNING" in guard_body
    assert "show_power_warning(page)" in guard_body

    warning_start = CONTROLLER.index("void WiFi::handle_power_warning_key")
    warning_end = CONTROLLER.index("void WiFi::start_scan", warning_start)
    warning_body = CONTROLLER[warning_start:warning_end]
    assert "access.set_view(SetupViewState::SUB)" in warning_body
    assert "access.select_sub(0, 3)" in warning_body
    assert "case ViewState::WIFI_POWER_WARNING:" in INPUT
    assert "wifi_.handle_power_warning_key(*this, key)" in INPUT

    start = CONTROLLER.index("void WiFi::start_scan(UISetupPage &page)")
    end = CONTROLLER.index("void WiFi::stop_scan()", start)
    body = CONTROLLER[start:end]
    assert "cp0_wifi_radio_set_enabled" not in body
    assert "cp0_wifi_radio_enabled()" in body
    assert "CP0_WIFI_ERROR_RADIO_OFF" in body
    assert body.index("cp0_wifi_radio_enabled()") < body.index("cp0_wifi_scan(")


if __name__ == "__main__":
    test_owned_wifi_views_unmount_before_member_destruction()
    test_wifi_controller_unmounts_list_while_view_is_alive()
    test_scan_respects_user_disabled_radio()
