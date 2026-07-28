#!/usr/bin/env python3
import importlib.util
import sys
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[3]
module_path = repo / "scripts" / "debian_packager.py"
spec = importlib.util.spec_from_file_location("debian_packager", module_path)
packager = importlib.util.module_from_spec(spec)
assert spec.loader
sys.modules[spec.name] = packager
spec.loader.exec_module(packager)

with tempfile.TemporaryDirectory() as temp_dir:
    root = Path(temp_dir)
    project_dir = root / "projects" / "APPLaunch"
    src_dir = project_dir / "dist"
    images_dir = src_dir / "APPLaunch" / "share" / "images"
    source_images = project_dir / "APPLaunch" / "share" / "images"
    images_dir.mkdir(parents=True)
    source_images.mkdir(parents=True)
    expected_images = {
        "appstore.png",
        "appstore-shortcut-settings.png",
        "appstore-shortcut-install.png",
        "appstore-shortcut-detail.png",
        "store_wordmark.png",
        "store_arrow_left.png",
    }
    for name in expected_images:
        (images_dir / name).write_bytes(b"png")
    (source_images / "source-only.png").write_bytes(b"not a build output")
    (src_dir / "M5CardputerZero-APPLaunch").write_bytes(b"binary")

    config = packager.PackageConfig()
    package_root = root / "staging" / "debian-APPLaunch"
    paths = packager.Paths(
        repo_root=root,
        tool_dir=root / "scripts",
        project_dir=project_dir,
        src_dir=src_dir,
        output_dir=root / "out",
        work_dir=root / "staging",
        package_root=package_root,
        package_file=root / "out" / config.file_name,
    )
    packager.prepare_package_tree(config, paths)

    packaged_images = package_root / "usr" / "share" / "APPLaunch" / "share" / "images"
    assert all((packaged_images / name).is_file() for name in expected_images)
    assert not (packaged_images / "source-only.png").exists()
    assert (package_root / "usr/libexec/applaunch-updater").stat().st_mode & 0o111
    assert (package_root / "usr/lib/systemd/system/applaunch-updater.service").is_file()
    assert (package_root / "usr/lib/systemd/system/applaunch-apt-update.service").is_file()
    assert (package_root / "usr/share/polkit-1/rules.d/60-applaunch-updater.rules").is_file()
