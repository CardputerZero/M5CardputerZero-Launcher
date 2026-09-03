#include "../main/ui/model/preinstalled_app_manifest.hpp"

#include <cassert>

int main()
{
    const auto entries = parse_preinstalled_app_manifest(
        "# release-owned desktop entries\n"
        "zclaw.desktop\tZClaw\tshare/images/claw_100.png\t/usr/share/APPLaunch/bin/ZClaw\tfalse\ttrue\r\n"
        "bad-name\tBad\tbad.png\t/usr/bin/bad\tfalse\tfalse\n"
        "missing-exec.desktop\tMissing\tmissing.png\t\tfalse\tfalse\n"
        "zclaw.desktop\tReplacement\treplacement.png\t/usr/bin/replacement\tfalse\tfalse\n"
        "downloaded.desktop\tDownloaded\tdownloaded.png\t/usr/bin/downloaded\tfalse\tfalse\textra-field\n"
        "bad-bool.desktop\tBad bool\tbad.png\t/usr/bin/bad\tFalse\tfalse\n");

    assert(entries.size() == 1);
    DesktopEntry zclaw{
        "ZClaw", "share/images/claw_100.png",
        "/usr/share/APPLaunch/bin/ZClaw", false, true};
    assert(preinstalled_app_manifest_contains(
        entries, "zclaw.desktop", zclaw));
    zclaw.name = "Impostor";
    assert(!preinstalled_app_manifest_contains(
        entries, "zclaw.desktop", zclaw));
    zclaw.name = "ZClaw";
    zclaw.icon = "share/images/impostor.png";
    assert(!preinstalled_app_manifest_contains(
        entries, "zclaw.desktop", zclaw));
    zclaw.icon = "share/images/claw_100.png";
    zclaw.terminal = true;
    assert(!preinstalled_app_manifest_contains(
        entries, "zclaw.desktop", zclaw));
    assert(!preinstalled_app_manifest_contains(
        entries, "downloaded.desktop", DesktopEntry{
            "Downloaded", "downloaded.png", "/usr/bin/downloaded", false, false}));
}
