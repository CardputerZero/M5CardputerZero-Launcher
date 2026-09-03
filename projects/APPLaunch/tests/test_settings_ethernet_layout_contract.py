import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/ui/settings/settings_system_page.cpp").read_text(
    encoding="utf-8"
)
HEADER = (ROOT / "main/ui/settings/settings_system_page.hpp").read_text(
    encoding="utf-8"
)


def function_body(signature: str) -> str:
    start = SOURCE.index(signature)
    opening_brace = SOURCE.index("{", start)
    depth = 0
    for index in range(opening_brace, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[opening_brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def normalized(source: str) -> str:
    return re.sub(r"\s+", " ", source).strip()


def layout_metric(name: str) -> int:
    match = re.search(rf"\b{name}\s*=\s*(\d+)", HEADER)
    assert match, f"missing layout metric: {name}"
    return int(match.group(1))


def test_table_columns_and_divider_fit_the_screen():
    row_factory = normalized(function_body("lv_obj_t *create_network_table_row"))

    assert "lv_obj_t *divider = lv_obj_create(row);" in row_factory
    assert re.search(
        r"lv_obj_set_pos\(divider,\s*metric\(LayoutMetric::TableDividerX\),\s*0\)",
        row_factory,
    )
    assert re.search(
        r"lv_obj_set_size\(divider,\s*metric\(LayoutMetric::TableDividerW\),\s*"
        r"metric\(LayoutMetric::TableDividerH\)\)",
        row_factory,
    )
    assert "lv_obj_set_style_bg_opa(divider, LV_OPA_COVER" in row_factory
    assert "lv_obj_set_style_border_width(divider, 0" in row_factory

    screen_width = layout_metric("ScreenW")
    table_x = layout_metric("TableX")
    table_width = layout_metric("TableW")
    key_right = layout_metric("TableKeyX") + layout_metric("TableKeyW")
    divider_x = layout_metric("TableDividerX")
    divider_right = divider_x + layout_metric("TableDividerW")
    value_x = layout_metric("TableValueX")
    value_right = value_x + layout_metric("TableValueW")

    assert table_x >= 0
    assert table_x + table_width <= screen_width
    assert key_right < divider_x < divider_right <= value_x
    assert value_right <= table_width
    assert layout_metric("TableDividerH") <= layout_metric("TableRowH")


def test_table_objects_are_static_and_values_are_readable():
    row_factory = normalized(function_body("lv_obj_t *create_network_table_row"))
    for object_name in ("row", "divider"):
        assert f"lv_obj_remove_flag({object_name}, LV_OBJ_FLAG_CLICKABLE);" in row_factory
        assert f"lv_obj_remove_flag({object_name}, LV_OBJ_FLAG_SCROLLABLE);" in row_factory

    assert "settings_fonts::sans(13, LV_FREETYPE_FONT_STYLE_BOLD)" in row_factory
    assert "settings_fonts::mono(14)" in row_factory

    render = normalized(
        function_body("void render_system_info(LvSettingSystemInfoPage3 *page)\n{")
    )
    assert "settings_fonts::mono(14)" in render
    assert "LV_LABEL_LONG_SCROLL_CIRCULAR" in render


def test_status_and_hint_have_fixed_non_overlapping_regions():
    screen_height = layout_metric("ScreenH")
    title_bottom = layout_metric("TitleY") + 16
    table_top = layout_metric("TableY")
    table_bottom = table_top + 3 * layout_metric("TableRowH")
    status_y = layout_metric("TableStatusY")
    status_bottom = status_y + layout_metric("TableStatusH")
    hint_y = layout_metric("HintY")
    hint_bottom = hint_y + layout_metric("TableHintH")

    assert title_bottom <= table_top
    assert table_bottom <= status_y
    assert status_bottom <= hint_y
    assert hint_bottom <= screen_height

    create_ui = normalized(function_body("void LvSettingSystemInfoPage3::create_ui"))
    assert "network_table ? settings_fonts::sans(11) : body_font()" in create_ui
    assert "network_table ? settings_fonts::sans(10, LV_FREETYPE_FONT_STYLE_BOLD)" in create_ui
    assert re.search(
        r"lv_obj_set_height\(\s*state_->status_label,\s*"
        r"LvSettingSystemInfoPage3::metric\(LayoutMetric::TableStatusH\)\s*\)",
        create_ui,
    )
    assert re.search(
        r"lv_label_set_long_mode\(state_->status_label,\s*"
        r"LV_LABEL_LONG_SCROLL_CIRCULAR\)",
        create_ui,
    )
    assert re.search(
        r"lv_obj_set_height\(\s*state_->hint,\s*"
        r"LvSettingSystemInfoPage3::metric\(LayoutMetric::TableHintH\)\s*\)",
        create_ui,
    )
    assert re.search(
        r"lv_label_set_long_mode\(state_->hint,\s*LV_LABEL_LONG_CLIP\)",
        create_ui,
    )


if __name__ == "__main__":
    test_table_columns_and_divider_fit_the_screen()
    test_table_objects_are_static_and_values_are_readable()
    test_status_and_hint_have_fixed_non_overlapping_regions()
