import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WIFI_PAGE = (ROOT / "main/ui/settings/settings_wifi_page.cpp").read_text(
    encoding="utf-8"
)


def function_body(signature: str) -> str:
    start = WIFI_PAGE.index(signature)
    opening_brace = WIFI_PAGE.index("{", start)
    depth = 0
    for index in range(opening_brace, len(WIFI_PAGE)):
        if WIFI_PAGE[index] == "{":
            depth += 1
        elif WIFI_PAGE[index] == "}":
            depth -= 1
            if depth == 0:
                return WIFI_PAGE[opening_brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def string_literals(source: str) -> list[str]:
    literals = re.findall(r'"(?:\\.|[^"\\])*"', source)
    return [ast.literal_eval(literal) for literal in literals]


def test_initial_disabled_state_shows_the_wifi_warning():
    create_ui = function_body("void LvSettingWifiScanPage3::create_ui")
    disabled_branch = re.search(
        r"if\s*\(\s*!wifi_power_enabled_\s*\)\s*\{(?P<body>.*?)\}",
        create_ui,
        re.DOTALL,
    )
    assert disabled_branch, "missing disabled-WiFi branch"
    branch_body = disabled_branch.group("body")
    assert re.search(r"\brender\s*\(\s*\)\s*;", branch_body)
    assert re.search(r"\bshow_power_warning\s*\(\s*\)\s*;", branch_body)
    assert re.search(r"\breturn\s*;", branch_body)


def test_radio_off_scan_result_shows_the_wifi_warning():
    apply_result = function_body("void LvSettingWifiScanPage3::apply_scan_result")
    radio_off_checks = re.findall(
        r"if\s*\(\s*result\.count\s*==\s*CP0_WIFI_ERROR_RADIO_OFF\s*\)\s*"
        r"\{(?P<body>.*?)\}",
        apply_result,
        re.DOTALL,
    )
    assert any("show_power_warning" in body for body in radio_off_checks)

    scan_errors = function_body("const char *LvSettingWifiScanPage3::scan_error_message")
    assert "WiFi is off. Enable WiFi to scan" in string_literals(scan_errors)


def test_wifi_warning_refers_to_the_enable_control():
    warning = function_body("void LvSettingWifiScanPage3::show_power_warning")
    warning_copy = string_literals(warning)
    assert "WiFi is disabled" in warning_copy
    assert "Enable WiFi before continuing." in warning_copy

    # Internal member names may retain "power"; this contract covers text that
    # can actually reach the UI through a C++ string literal.
    assert not [text for text in string_literals(WIFI_PAGE) if "power" in text.lower()]


if __name__ == "__main__":
    test_initial_disabled_state_shows_the_wifi_warning()
    test_radio_off_scan_result_shows_the_wifi_warning()
    test_wifi_warning_refers_to_the_enable_control()
