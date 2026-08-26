from pathlib import Path


SOURCE = (
    Path(__file__).resolve().parents[1]
    / "main/ui/ui_launch_page_carousel_view.cpp"
).read_text(encoding="utf-8")


def test_every_carousel_card_keeps_the_theme_border():
    start = SOURCE.index("lv_obj_t *create_carousel_card")
    end = SOURCE.index("lv_obj_t *create_carousel_title", start)
    factory = SOURCE[start:end]
    assert "lv_obj_set_style_border_width(card," not in factory
    assert "lv_obj_set_style_border_width(elements[kCardCenter]" not in SOURCE
    assert "lv_obj_set_style_border_post(card, true," in factory
    assert "lv_obj_set_style_clip_corner(card, true," in factory


if __name__ == "__main__":
    test_every_carousel_card_keeps_the_theme_border()
