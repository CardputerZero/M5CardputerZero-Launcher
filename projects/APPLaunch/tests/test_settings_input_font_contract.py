from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WIFI = (ROOT / "main/ui/settings/settings_wifi_page.hpp").read_text()
BLUETOOTH = (ROOT / "main/ui/settings/settings_bluetooth_page.hpp").read_text()


def input_font_helper(source: str) -> str:
    start = source.index("static const lv_font_t *input_font")
    end = source.index("\n    }", start) + len("\n    }")
    return source[start:end]


def test_editors_use_regular_bitmap_freetype_style():
    for source in (WIFI, BLUETOOTH):
        helper = input_font_helper(source)
        assert '"AlibabaPuHuiTi-3-55-Regular.ttf"' in helper
        assert "LV_FREETYPE_FONT_STYLE_NORMAL" in helper
        assert "LV_FREETYPE_FONT_RENDER_MODE_BITMAP" in helper
        assert "LV_FREETYPE_FONT_STYLE_BOLD" not in helper


def test_wifi_editors_share_the_configured_input_font():
    password_panel = WIFI[WIFI.index("void create_password_panel") :]
    password_panel = password_panel[: password_panel.index("void render_password_editor")]
    assert password_panel.count("input_font(16)") == 3

    hidden_input = WIFI[WIFI.index("static lv_obj_t *create_hidden_input") :]
    hidden_input = hidden_input[: hidden_input.index("void create_hidden_network_panel")]
    assert "lv_textarea_create(parent)" in hidden_input
    assert "input_font(14)" in hidden_input
    assert "settings_text_cursor" not in WIFI


def test_hidden_wifi_cursor_is_centered_in_explicit_character_spacing():
    hidden_input = WIFI[WIFI.index("static lv_obj_t *create_hidden_input") :]
    hidden_input = hidden_input[: hidden_input.index("void create_hidden_network_panel")]

    assert "static constexpr int HIDDEN_INPUT_LETTER_SPACE = 1;" in WIFI
    assert "static constexpr int HIDDEN_INPUT_CURSOR_WIDTH = 1;" in WIFI
    assert (
        "lv_obj_set_style_text_letter_space(input, "
        "HIDDEN_INPUT_LETTER_SPACE, LV_PART_MAIN);"
    ) in hidden_input
    assert (
        "lv_obj_set_style_border_width(input, "
        "HIDDEN_INPUT_CURSOR_WIDTH, LV_PART_CURSOR);"
    ) in hidden_input
    assert "lv_obj_set_style_pad_left(input, -1, LV_PART_CURSOR);" in hidden_input

    focus_style = WIFI[WIFI.index("void set_hidden_focus") :]
    focus_style = focus_style[: focus_style.index("void render_hidden_network_panel")]
    assert (
        "lv_obj_set_style_border_width(focused, "
        "HIDDEN_INPUT_CURSOR_WIDTH, LV_PART_CURSOR);"
    ) in focus_style


def test_bluetooth_editor_uses_one_font_for_all_cursor_states():
    alias_editor = BLUETOOTH[BLUETOOTH.index("class LvSettingBluetoothAliasPage3") :]
    assert alias_editor.count("input_font(14)") == 3
    assert "settings_text_cursor" not in alias_editor


if __name__ == "__main__":
    test_editors_use_regular_bitmap_freetype_style()
    test_wifi_editors_share_the_configured_input_font()
    test_hidden_wifi_cursor_is_centered_in_explicit_character_spacing()
    test_bluetooth_editor_uses_one_font_for_all_cursor_states()
