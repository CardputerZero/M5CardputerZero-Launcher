from pathlib import Path


SOURCE = (
    Path(__file__).resolve().parents[1]
    / "main/ui/ui_launch_page_carousel_view.cpp"
).read_text(encoding="utf-8")
HEADER = (
    Path(__file__).resolve().parents[1]
    / "main/ui/ui_launch_page.h"
).read_text(encoding="utf-8")
LAUNCH = (
    Path(__file__).resolve().parents[1]
    / "main/ui/launch.cpp"
).read_text(encoding="utf-8")
POOL = (
    Path(__file__).resolve().parents[1]
    / "main/ui/home_icon_buffer_pool.cpp"
).read_text(encoding="utf-8")


def test_every_carousel_card_keeps_the_theme_border():
    start = SOURCE.index("lv_obj_t *create_carousel_card")
    end = SOURCE.index("lv_obj_t *create_carousel_title", start)
    factory = SOURCE[start:end]
    assert "lv_obj_set_style_border_width(card," not in factory
    assert "lv_obj_set_style_border_width(elements[kCardCenter]" not in SOURCE
    assert "lv_obj_set_style_border_post(card, true," in factory
    assert "lv_obj_set_style_clip_corner(card," not in factory


def test_carousel_switch_uses_only_preloaded_images():
    assert "HomeIconBufferPool home_icon_pool_" in HEADER
    assert "home_icon_pool_.find(src, image_size)" in SOURCE
    assert "lv_image_set_src(image, icon_src)" not in SOURCE
    assert "lv_image_decoder_open" not in SOURCE
    assert "lv_image_decoder_open" in POOL


def test_icon_pool_can_prepare_exact_render_sizes():
    assert "resized_icons_" in POOL
    assert "decode_and_prepare(path, size)" in POOL
    assert "lv_draw_buf_create(size, size" in POOL


def test_icon_pool_rebuilds_with_the_app_list():
    bind_start = LAUNCH.index("void Launch::bind_ui()")
    bind_end = LAUNCH.index("void Launch::launch_app()", bind_start)
    reload_start = LAUNCH.index("void Launch::applications_reload()")
    reload_end = LAUNCH.index("int Launch::normalized_app_index", reload_start)
    assert "reload_home_icons();" in LAUNCH[bind_start:bind_end]
    assert "reload_home_icons();" in LAUNCH[reload_start:reload_end]


def test_icons_are_clipped_by_their_card_content_geometry():
    start = SOURCE.index("void UILaunchPage::set_panel_icon")
    end = SOURCE.index("void UILaunchPage::update_carousel_slot", start)
    setter = SOURCE[start:end]
    assert "lv_obj_t *clip" in setter
    assert "lv_obj_create(panel)" in setter
    assert "lv_obj_set_style_radius(clip" in setter
    assert "lv_obj_set_style_clip_corner(clip, true" in setter
    assert "apply_rounded_corners" not in POOL
    assert "lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER)" in setter
    assert "compensate_image_scale(image)" in setter


def test_icon_scale_rounds_up_during_carousel_animation():
    animation = (
        Path(__file__).resolve().parents[1]
        / "main/ui/animation/ui_launcher_animation.cpp"
    ).read_text(encoding="utf-8")
    assert "lv_image_set_scale_x(image, scale_x)" in animation
    assert "lv_image_set_scale_y(image, scale_y)" in animation
    assert "compensate_image_scale(obj);" in animation


if __name__ == "__main__":
    test_every_carousel_card_keeps_the_theme_border()
    test_carousel_switch_uses_only_preloaded_images()
    test_icon_pool_can_prepare_exact_render_sizes()
    test_icon_pool_rebuilds_with_the_app_list()
    test_icons_are_clipped_by_their_card_content_geometry()
    test_icon_scale_rounds_up_during_carousel_animation()
