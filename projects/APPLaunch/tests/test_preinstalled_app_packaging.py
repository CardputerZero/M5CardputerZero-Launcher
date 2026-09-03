import subprocess
import sys
import tempfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PROJECT_ROOT.parents[1]
RELEASE_SCRIPT = (
    REPOSITORY_ROOT / "scripts/build_cardputerzero_release_local.sh"
).read_text(encoding="utf-8")
MANIFEST = (
    PROJECT_ROOT / "APPLaunch/preinstalled-desktop-apps.tsv"
).read_text(encoding="utf-8")
IDENTITY_VERIFIER = PROJECT_ROOT / "build_support/verify_preinstalled_desktop.py"


def test_release_aggregation_validates_preinstalled_desktop_identity():
    identity = (
        "zclaw.desktop\tZClaw\tshare/images/claw_100.png\t"
        "/usr/share/APPLaunch/bin/ZClaw\tfalse\ttrue"
    )
    assert identity in MANIFEST
    assert 'cp -a "$ROOT/projects/ZClaw/dist/APPLaunch/."' in RELEASE_SCRIPT
    assert "PREINSTALLED_MANIFEST=" in RELEASE_SCRIPT
    assert "ZCLAW_DESKTOP=" in RELEASE_SCRIPT
    assert "build_support/verify_preinstalled_desktop.py" in RELEASE_SCRIPT
    assert "./usr/share/APPLaunch/preinstalled-desktop-apps.tsv$" in RELEASE_SCRIPT


def run_verifier(manifest: str, desktop: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        manifest_path = root / "preinstalled-desktop-apps.tsv"
        desktop_path = root / "zclaw.desktop"
        manifest_path.write_text(manifest, encoding="utf-8")
        desktop_path.write_text(desktop, encoding="utf-8")
        return subprocess.run(
            [
                sys.executable,
                str(IDENTITY_VERIFIER),
                str(manifest_path),
                str(desktop_path),
                "zclaw.desktop",
            ],
            capture_output=True,
            text=True,
            check=False,
        )


def test_identity_verifier_accepts_matching_desktop_defaults():
    result = run_verifier(
        "zclaw.desktop\tZClaw\tclaw.png\t/usr/bin/zclaw\tfalse\ttrue\n",
        "[Desktop Entry]\nName=ZClaw\nIcon=claw.png\nExec=/usr/bin/zclaw\nType=Application\n",
    )
    assert result.returncode == 0, result.stderr


def test_identity_verifier_rejects_desktop_drift():
    result = run_verifier(
        "zclaw.desktop\tZClaw\tclaw.png\t/usr/bin/zclaw\tfalse\ttrue\n",
        "[Desktop Entry]\nName=ZClaw Next\nIcon=claw.png\nExec=/usr/bin/zclaw\n",
    )
    assert result.returncode != 0
    assert "Name: manifest='ZClaw', desktop='ZClaw Next'" in result.stderr


if __name__ == "__main__":
    test_release_aggregation_validates_preinstalled_desktop_identity()
    test_identity_verifier_accepts_matching_desktop_defaults()
    test_identity_verifier_rejects_desktop_drift()
