from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WIFI_HEADER = (ROOT / "main/ui/settings/settings_wifi_page.hpp").read_text()
WIFI_SOURCE = (ROOT / "main/ui/settings/settings_wifi_page.cpp").read_text()
BLUETOOTH_HEADER = (ROOT / "main/ui/settings/settings_bluetooth_page.hpp").read_text()
BLUETOOTH_SOURCE = (ROOT / "main/ui/settings/settings_bluetooth_page.cpp").read_text()
CP0_INCLUDE = ROOT.parents[1] / "ext_components/cp0_lvgl/include"
SETTINGS_FONTS = (CP0_INCLUDE / "settings_fonts.hpp").read_text()
FONT_SERVICE = (CP0_INCLUDE / "cp0_font_service.hpp").read_text()


def function_definition(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_editors_use_regular_bitmap_freetype_style():
    helpers = (
        function_definition(
            WIFI_SOURCE, "const lv_font_t *LvSettingWifiScanPage3::input_font"
        ),
        function_definition(
            BLUETOOTH_SOURCE,
            "const lv_font_t *LvSettingBluetoothAliasPage3::input_font",
        ),
    )
    for helper in helpers:
        assert "return settings_fonts::cjk_sans(size);" in helper
        assert "LV_FREETYPE_FONT_STYLE_BOLD" not in helper

    cjk_sans = function_definition(
        SETTINGS_FONTS, "inline const lv_font_t *cjk_sans"
    )
    assert "LV_FREETYPE_FONT_STYLE_NORMAL" in cjk_sans
    assert '"NotoSansCJK-Regular.ttc"' in cjk_sans
    assert '"AlibabaPuHuiTi-3-55-Regular.ttf"' in cjk_sans

    get_start = FONT_SERVICE.index("lv_font_t *get(")
    get_declaration = FONT_SERVICE[
        get_start : FONT_SERVICE.index(");", get_start) + 2
    ]
    assert "LV_FREETYPE_FONT_RENDER_MODE_BITMAP" in get_declaration


def test_wifi_editors_share_the_configured_input_font():
    password_panel = function_definition(
        WIFI_SOURCE, "void LvSettingWifiScanPage3::create_password_panel"
    )
    hidden_panel = function_definition(
        WIFI_SOURCE, "void LvSettingWifiScanPage3::create_hidden_network_panel"
    )
    hidden_input = function_definition(
        WIFI_SOURCE, "lv_obj_t *LvSettingWifiScanPage3::create_hidden_input"
    )
    assert password_panel.count("create_hidden_input(") == 1
    assert hidden_panel.count("create_hidden_input(") == 2
    assert "lv_textarea_create(parent)" in hidden_input
    assert "input_font(14)" in hidden_input
    assert "settings_text_cursor" not in WIFI_HEADER + WIFI_SOURCE


def test_hidden_wifi_cursor_is_centered_in_explicit_character_spacing():
    hidden_input = function_definition(
        WIFI_SOURCE, "lv_obj_t *LvSettingWifiScanPage3::create_hidden_input"
    )

    assert "HiddenInputLetterSpace = 1" in WIFI_HEADER
    assert "HiddenInputCursorWidth = 1" in WIFI_HEADER
    assert "metric(LayoutMetric::HiddenInputLetterSpace), LV_PART_MAIN" in hidden_input
    assert "metric(LayoutMetric::HiddenInputCursorWidth), LV_PART_CURSOR" in hidden_input
    assert "lv_obj_set_style_pad_left(input, -1, LV_PART_CURSOR);" in hidden_input

    focus_style = function_definition(
        WIFI_SOURCE, "void LvSettingWifiScanPage3::set_hidden_focus"
    )
    assert "metric(LayoutMetric::HiddenInputCursorWidth), LV_PART_CURSOR" in focus_style


def test_bluetooth_editor_uses_one_font_for_all_cursor_states():
    alias_editor = function_definition(
        BLUETOOTH_SOURCE, "void LvSettingBluetoothAliasPage3::render"
    )
    assert alias_editor.count("input_font(14)") == 1
    assert "lv_obj_set_style_text_font(alias_input_, input_font(14), LV_PART_MAIN);" in alias_editor
    assert "settings_text_cursor" not in BLUETOOTH_HEADER + BLUETOOTH_SOURCE


if __name__ == "__main__":
    test_editors_use_regular_bitmap_freetype_style()
    test_wifi_editors_share_the_configured_input_font()
    test_hidden_wifi_cursor_is_centered_in_explicit_character_spacing()
    test_bluetooth_editor_uses_one_font_for_all_cursor_states()
