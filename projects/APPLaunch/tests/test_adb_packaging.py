#!/usr/bin/env python3
import importlib.util
import subprocess
import sys
from pathlib import Path

repo = Path(__file__).resolve().parents[3]
module_path = repo / "scripts" / "debian_packager.py"
spec = importlib.util.spec_from_file_location("debian_packager", module_path)
packager = importlib.util.module_from_spec(spec)
assert spec.loader
sys.modules[spec.name] = packager
spec.loader.exec_module(packager)

postinst = packager._postinst_text(packager.PackageConfig())
assert "/usr/share/APPLaunch/adb/cardputer-adb" in postinst
assert '"$ADB_HELPER" migrate' in postinst
assert 'user_systemctl restart "$SERVICE_NAME" || user_systemctl start "$SERVICE_NAME" || true' not in postinst

system_postinst = packager._postinst_text(packager.PackageConfig(service_scope="system"))
assert '"$ADB_HELPER" migrate' in system_postinst

other = packager.PackageConfig(app_name="OtherApp", package_name="other")
assert "cardputer-adb" not in packager._postinst_text(other)

prerm = packager._prerm_text(packager.PackageConfig())
case_pos = prerm.index('case "$1" in')
assert prerm.index('user_systemctl stop "$SERVICE_NAME"', case_pos) > case_pos
assert prerm.index('rm -rf /var/cache/APPLaunch', case_pos) > case_pos

updater = packager._updater_script_text()
subprocess.run(["sh", "-n"], input=updater, text=True, check=True)
assert "dpkg -i \"$package\"" in updater
assert "dpkg --configure -a" in updater
assert "rollback.deb" in updater
assert "APPLAUNCH_UPDATER_REEXEC" in updater
assert "is-active --quiet APPLaunch.service" in updater
assert "succeeded:$candidate" in updater

policy = packager._updater_polkit_text()
assert 'action.lookup("unit") == "applaunch-updater.service"' in policy
assert 'action.lookup("verb") == "start"' in policy
assert 'action.lookup("verb") == "stop"' in policy
assert 'action.lookup("unit") == "applaunch-apt-update.service"' in policy
assert "subject.local && subject.active" in policy

service = packager._updater_service_text()
assert "Type=oneshot" in service
assert "ExecStart=/usr/libexec/applaunch-updater" in service
assert "applaunch_arm64.deb" not in service

apt_service = packager._apt_update_service_text()
assert "Type=oneshot" in apt_service
assert "ExecStart=/usr/bin/apt update" in apt_service
