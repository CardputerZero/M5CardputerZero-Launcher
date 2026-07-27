from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "main/ui/page_app/setting/wifi.hpp").read_text(encoding="utf-8")
SOURCE = (ROOT / "main/ui/page_app/setting/wifi_view.cpp").read_text(encoding="utf-8")
CONTROLLER = (ROOT / "main/ui/page_app/setting/wifi.cpp").read_text(encoding="utf-8")


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


if __name__ == "__main__":
    test_owned_wifi_views_unmount_before_member_destruction()
    test_wifi_controller_unmounts_list_while_view_is_alive()
