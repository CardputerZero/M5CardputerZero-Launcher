from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/ui/ui_screensaver.cpp").read_text()
MODEL = (ROOT / "main/ui/model/screensaver_model.hpp").read_text()
IMAGE = ROOT / "APPLaunch/share/images/screensaver.png"


assert "enum class BlockMetric : int" in MODEL
assert "Size = 50" in MODEL
assert "enum class ColorMetric : std::size_t" in MODEL
assert "Count = 8" in MODEL
assert "launcher_platform::path(\"screensaver.png\")" in SOURCE
assert "lv_image_decoder_open(&decoder, path.c_str(), &args)" in SOURCE
assert "args.no_cache = true;" in SOURCE
assert "lv_draw_buf_create(" in SOURCE
assert "s_image_cache.image()" in SOURCE
assert "SCREEN_ICON_PIXELS" not in SOURCE
assert "DEFINE_SCREEN_IMAGE" not in SOURCE
assert IMAGE.is_file()
assert IMAGE.read_bytes()[16:24] == struct.pack(">II", 50, 50)
