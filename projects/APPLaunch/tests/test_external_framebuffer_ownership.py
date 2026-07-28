#!/usr/bin/env python3
from pathlib import Path


source = (Path(__file__).resolve().parents[1] / "main/ui/launch.cpp").read_text(
    encoding="utf-8"
)
start = source.index("void Launch::launch_Exec(")
end = source.index("\nvoid Launch::select_next_app()", start)
body = source[start:end]

paint = body.index("lv_refr_now(disp)")
pause = body.index("lv_timer_enable(false)")
execute = body.index('"ExecBlocking"')
resume = body.index("lv_timer_enable(true)")
restore = body.index("page->show_home_screen()")

assert paint < pause < execute < resume < restore
assert "lv_timer_create" not in body
assert "std::thread" not in body
assert "launcher_toast" not in body
