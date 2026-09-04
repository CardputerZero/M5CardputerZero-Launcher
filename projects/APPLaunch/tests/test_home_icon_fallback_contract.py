from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POOL = (ROOT / "main/ui/home_icon_buffer_pool.cpp").read_text(encoding="utf-8")
ASSET = (ROOT / "main/ui/screensaver_fallback.c").read_text(encoding="utf-8")


assert '#include "screensaver_fallback.h"' in POOL
assert "return found == icons_.end() ? &screensaver_fallback" in POOL
assert "return &screensaver_fallback;" in POOL
assert "lv_color32_make(0x44, 0x44, 0x44" not in POOL
assert "const lv_image_dsc_t screensaver_fallback" in ASSET
assert "uint8_t screensaver_fallback_map[]" in ASSET
assert ".cf = LV_COLOR_FORMAT_ARGB8888" in ASSET
assert ".w = 50" in ASSET
assert ".h = 50" in ASSET
assert ".stride = 200" in ASSET


if __name__ == "__main__":
    pass
