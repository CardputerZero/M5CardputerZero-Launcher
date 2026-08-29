#!/usr/bin/env python3
"""Build project Debian packages on Linux, macOS, or Windows.

APPLaunch remains the default target, while CLI options allow other projects in
this repository to reuse the same cross-platform package builder.
"""

from __future__ import annotations

import argparse
import io
import os
import platform
import shutil
import subprocess
import sys
import tarfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Iterable, Sequence


DEFAULT_PROJECT = "APPLaunch"
PACKAGE_NAME = "applaunch"
APP_NAME = "APPLaunch"
BIN_NAME = "M5CardputerZero-APPLaunch"
DEFAULT_VERSION = "0.6.1"
DEFAULT_REVISION = "m5stack1"
DEFAULT_ARCHITECTURE = "arm64"

INSTALL_PARENT = PurePosixPath("usr/share")
SERVICE_PATH = PurePosixPath("usr/lib/systemd/user")
SYSTEM_SERVICE_PATH = PurePosixPath("usr/lib/systemd/system")
POLKIT_RULES_PATH = PurePosixPath("usr/share/polkit-1/rules.d")
LIBEXEC_PATH = PurePosixPath("usr/libexec")
LAUNCHER_UPDATE_ABI = "1"

OPTIONAL_BINARIES = (
    "M5CardputerZero-AppStore",
    "appstore.py",
    "M5CardputerZero-Calculator",
    "ZClaw",
)


class PackError(RuntimeError):
    """Raised when the package cannot be assembled."""


@dataclass(frozen=True)
class PackageConfig:
    version: str = DEFAULT_VERSION
    revision: str = DEFAULT_REVISION
    architecture: str = DEFAULT_ARCHITECTURE
    package_name: str = PACKAGE_NAME
    app_name: str = APP_NAME
    bin_name: str = BIN_NAME
    maintainer: str = "dianjixz <dianjixz@m5stack.com>"
    original_maintainer: str = "m5stack <m5stack@m5stack.com>"
    section: str = APP_NAME
    priority: str = "optional"
    homepage: str = "https://www.m5stack.com"
    description: str = "M5CardputerZero APPLaunch"
    service_scope: str = "user"
    service_restart: str = "always"

    @property
    def install_prefix(self) -> PurePosixPath:
        return INSTALL_PARENT / self.app_name

    @property
    def bin_path(self) -> PurePosixPath:
        return self.install_prefix / "bin"

    @property
    def service_path(self) -> PurePosixPath:
        return SYSTEM_SERVICE_PATH if self.service_scope == "system" else SERVICE_PATH

    @property
    def file_name(self) -> str:
        return f"{self.package_name}_{self.version}-{self.revision}_{self.architecture}.deb"


@dataclass(frozen=True)
class Paths:
    repo_root: Path
    tool_dir: Path
    project_dir: Path
    src_dir: Path
    output_dir: Path
    work_dir: Path
    package_root: Path
    package_file: Path


def _posix_path(path: PurePosixPath | str) -> str:
    return str(path).replace("\\", "/")


def _resolve_path(path: str | os.PathLike[str], base: Path) -> Path:
    candidate = Path(path).expanduser()
    if not candidate.is_absolute():
        candidate = base / candidate
    return candidate.resolve()


def _safe_remove(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def _chmod(path: Path, mode: int) -> None:
    try:
        path.chmod(mode)
    except PermissionError:
        if platform.system() != "Windows":
            raise


def _mkdir(root: Path, relative: PurePosixPath | str) -> Path:
    target = root / Path(*PurePosixPath(relative).parts)
    target.mkdir(parents=True, exist_ok=True)
    return target


def _copy_file(src: Path, dst: Path, mode: int | None = None) -> None:
    if not src.is_file():
        raise PackError(f"required file not found: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    if mode is not None:
        _chmod(dst, mode)


def _find_binary(src_dir: Path, bin_name: str) -> Path:
    for candidate in (src_dir / bin_name, src_dir / "bin" / bin_name):
        if candidate.is_file():
            return candidate
    raise PackError(f"binary {bin_name} not found in {src_dir} or {src_dir / 'bin'}")


def _default_app_tree(src_dir: Path, app_name: str) -> Path:
    candidate = src_dir / app_name
    if candidate.is_dir():
        return candidate
    raise PackError(f"{app_name} resource tree not found: {candidate}")


def _copy_tree(src: Path, dst: Path) -> None:
    if not src.is_dir():
        raise PackError(f"required directory not found: {src}")
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, symlinks=True)


def _copy_optional_binaries(src_dir: Path, package_root: Path, config: PackageConfig) -> list[str]:
    copied: list[str] = []
    for name in OPTIONAL_BINARIES:
        candidates = (src_dir / "bin" / name, src_dir / name)
        source = next((path for path in candidates if path.is_file()), None)
        if source is None:
            continue
        mode = 0o644 if name.endswith(".py") else 0o755
        _copy_file(source, package_root / Path(*config.bin_path.parts) / name, mode=mode)
        copied.append(name)
    return copied


def _source_date_epoch() -> int:
    value = os.environ.get("SOURCE_DATE_EPOCH")
    if value is None:
        return int(datetime.now(timezone.utc).timestamp())
    try:
        epoch = int(value)
    except ValueError as exc:
        raise PackError("SOURCE_DATE_EPOCH must be an integer") from exc
    if epoch < 0:
        raise PackError("SOURCE_DATE_EPOCH must not be negative")
    return epoch


def _control_text(config: PackageConfig) -> str:
    packaged_at = datetime.fromtimestamp(_source_date_epoch(), timezone.utc)
    fields = {
        "Package": config.package_name,
        "Version": config.version,
        "Architecture": config.architecture,
        "Maintainer": config.maintainer,
        "Original-Maintainer": config.original_maintainer,
        "Section": config.section,
        "Priority": config.priority,
        "Homepage": config.homepage,
        "Packaged-Date": packaged_at.strftime("%Y-%m-%d %H:%M:%S UTC"),
        "Description": config.description,
    }
    if config.app_name == APP_NAME and config.package_name == PACKAGE_NAME:
        fields["X-CardputerZero-Update-ABI"] = LAUNCHER_UPDATE_ABI
        # The user service pre-creates XDG folders (Music, Pictures, ...) via
        # xdg-user-dirs-update; without a desktop session nothing else does.
        fields["Depends"] = "xdg-user-dirs"
    return "".join(f"{key}: {value}\n" for key, value in fields.items())


def _postinst_text(config: PackageConfig) -> str:
    service_file = f"/{_posix_path(config.service_path / f'{config.app_name}.service')}"
    service_name = f"{config.app_name}.service"
    adb_migration = ""
    if config.app_name == APP_NAME:
        adb_migration = f"""
ADB_HELPER=/{_posix_path(config.install_prefix / 'adb/cardputer-adb')}
if [ -x "$ADB_HELPER" ]; then
    "$ADB_HELPER" migrate
fi
"""
    if config.service_scope == "system":
        return f"""#!/bin/sh
set -e
mkdir -p /var/cache/{config.app_name}
chown 1000:1000 /var/cache/{config.app_name} || true
ln -sfn /var/cache/{config.app_name} /usr/share/{config.app_name}/cache
SERVICE_NAME="{service_name}"
SERVICE_FILE="{service_file}"
{adb_migration}

systemd_is_running() {{
    [ -d /run/systemd/system ] && [ "$(ps -p 1 -o comm= 2>/dev/null)" = "systemd" ]
}}

if command -v systemctl >/dev/null 2>&1 && [ -f "$SERVICE_FILE" ]; then
    systemctl daemon-reload
    systemctl enable "$SERVICE_NAME"
    if systemd_is_running; then
        systemctl restart "$SERVICE_NAME" || systemctl start "$SERVICE_NAME"
    else
        echo "{config.app_name}: systemd is not running; enabled system service for first boot" >&2
    fi
else
    echo "{config.app_name}: systemctl unavailable or service file missing; skip system service enable/start" >&2
fi
exit 0
"""
    return f"""#!/bin/sh
set -e
mkdir -p /var/cache/{config.app_name}
chown 1000:1000 /var/cache/{config.app_name} || true
ln -sfn /var/cache/{config.app_name} /usr/share/{config.app_name}/cache
	APP_UID=1000
	APP_USER="$(getent passwd "$APP_UID" | cut -d: -f1)"
	APP_GID="$(getent passwd "$APP_UID" | cut -d: -f4)"
	APP_HOME="$(getent passwd "$APP_UID" | cut -d: -f6)"
	SERVICE_NAME="{service_name}"
	SERVICE_FILE="{service_file}"
	{adb_migration}

systemd_is_running() {{
    [ -d /run/systemd/system ] && [ "$(ps -p 1 -o comm= 2>/dev/null)" = "systemd" ]
}}

	user_systemctl() {{
    runuser -u "$APP_USER" -- env \\
        XDG_RUNTIME_DIR="/run/user/$APP_UID" \\
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$APP_UID/bus" \\
	        systemctl --user "$@"
	}}

	enable_user_service_offline() {{
	    USER_WANTS="$APP_HOME/.config/systemd/user/default.target.wants"
	    install -d -m 0755 -o "$APP_UID" -g "$APP_GID" "$USER_WANTS"
	    ln -sfn "$SERVICE_FILE" "$USER_WANTS/$SERVICE_NAME"
	    chown -h "$APP_UID:$APP_GID" "$USER_WANTS/$SERVICE_NAME"
	}}

	if command -v systemctl >/dev/null 2>&1 && [ -f "$SERVICE_FILE" ]; then
	    # A global user-service link starts the launcher in greeter and service
	    # accounts too. Remove legacy global enablement and scope it to UID 1000.
	    systemctl --global disable "$SERVICE_NAME" || true
	    if systemd_is_running && [ -n "$APP_USER" ]; then
        if command -v loginctl >/dev/null 2>&1; then
            loginctl enable-linger "$APP_USER" || true
        fi
        systemctl daemon-reload
        systemctl start "user@$APP_UID.service"
        user_systemctl daemon-reload
	        user_systemctl enable "$SERVICE_NAME"
	        user_systemctl restart "$SERVICE_NAME" || user_systemctl start "$SERVICE_NAME"
	    elif [ -n "$APP_USER" ]; then
	        enable_user_service_offline
	        echo "{config.app_name}: enabled user service for UID $APP_UID on first boot" >&2
	    else
	        echo "{config.app_name}: UID 1000 user not found; skip user service enable/start" >&2
	    fi
else
    echo "{config.app_name}: systemctl unavailable or service file missing; skip user service enable/start" >&2
fi
exit 0
"""


def _prerm_text(config: PackageConfig) -> str:
    service_file = f"/{_posix_path(config.service_path / f'{config.app_name}.service')}"
    service_name = f"{config.app_name}.service"
    if config.service_scope == "system":
        return f"""#!/bin/sh
set -e
SERVICE_NAME="{service_name}"
SERVICE_FILE="{service_file}"

systemd_is_running() {{
    [ -d /run/systemd/system ] && [ "$(ps -p 1 -o comm= 2>/dev/null)" = "systemd" ]
}}

case "$1" in
    remove|deconfigure)
        if command -v systemctl >/dev/null 2>&1 && [ -f "$SERVICE_FILE" ]; then
            if systemd_is_running; then
                systemctl stop "$SERVICE_NAME" || true
            fi
            systemctl disable "$SERVICE_NAME" || true
        fi
        rm -rf /var/cache/{config.app_name}
        ;;
esac
exit 0
"""
    return f"""#!/bin/sh
set -e
	APP_UID=1000
	APP_USER="$(getent passwd "$APP_UID" | cut -d: -f1)"
	APP_HOME="$(getent passwd "$APP_UID" | cut -d: -f6)"
SERVICE_NAME="{service_name}"
SERVICE_FILE="{service_file}"

systemd_is_running() {{
    [ -d /run/systemd/system ] && [ "$(ps -p 1 -o comm= 2>/dev/null)" = "systemd" ]
}}

user_systemctl() {{
    runuser -u "$APP_USER" -- env \\
        XDG_RUNTIME_DIR="/run/user/$APP_UID" \\
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$APP_UID/bus" \\
        systemctl --user "$@"
}}

case "$1" in
    remove|deconfigure)
        if command -v systemctl >/dev/null 2>&1 && [ -f "$SERVICE_FILE" ]; then
            if systemd_is_running && [ -n "$APP_USER" ]; then
                user_systemctl stop "$SERVICE_NAME" || true
                user_systemctl disable "$SERVICE_NAME" || true
            fi
	            systemctl --global disable "$SERVICE_NAME" || true
	            if [ -n "$APP_HOME" ]; then
	                rm -f "$APP_HOME/.config/systemd/user/default.target.wants/$SERVICE_NAME"
	            fi
        fi
        rm -rf /var/cache/{config.app_name}
        ;;
esac
exit 0
"""


def _updater_service_text() -> str:
    return """[Unit]
Description=APPLaunch trusted package updater
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/libexec/applaunch-updater
TimeoutStartSec=20min
TimeoutStopSec=5min
Nice=10
IOSchedulingClass=best-effort
IOSchedulingPriority=7
"""


def _apt_update_service_text() -> str:
    return """[Unit]
Description=APPLaunch trusted package index refresh
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/bin/apt update
TimeoutStartSec=5min
Nice=10
"""


def _updater_polkit_text() -> str:
    return """// Permit a local administrator to start only the fixed updater units.
// APPLaunch runs as a systemd user service, so it is not attached to the
// active seat even though it owns the physical device UI.
polkit.addRule(function(action, subject) {
    if (action.id == "org.freedesktop.systemd1.manage-units" &&
        (action.lookup("unit") == "applaunch-updater.service" ||
         action.lookup("unit") == "applaunch-apt-update.service") &&
        (action.lookup("verb") == "start" ||
         (action.lookup("verb") == "stop" &&
          (action.lookup("unit") == "applaunch-updater.service" ||
           action.lookup("unit") == "applaunch-apt-update.service"))) &&
        subject.isInGroup("sudo")) {
        return polkit.Result.YES;
    }
});
"""


def _updater_script_text() -> str:
    return r"""#!/bin/sh
set -eu

# dpkg replaces this package's files during the transaction.  Re-exec from a
# private runtime copy so replacing /usr/libexec/applaunch-updater cannot alter
# the script that is currently coordinating install and rollback.
if [ "${APPLAUNCH_UPDATER_REEXEC:-0}" != 1 ]; then
    state_root=${APPLAUNCH_UPDATE_STATE_DIR:-/var/lib/applaunch-updater}
    install -d -m 0755 "$state_root"
    self_dir=$(mktemp -d "$state_root/self.XXXXXX")
    install -m 0700 "$0" "$self_dir/updater"
    exec env APPLAUNCH_UPDATER_REEXEC=1 APPLAUNCH_UPDATER_SELF_DIR="$self_dir" \
        "$self_dir/updater"
fi
SELF_DIR=${APPLAUNCH_UPDATER_SELF_DIR:-}

PACKAGE_NAME=${APPLAUNCH_UPDATE_PACKAGE_NAME:-applaunch}
ARCHITECTURE=${APPLAUNCH_UPDATE_ARCHITECTURE:-arm64}
UPDATE_ABI=${APPLAUNCH_UPDATE_ABI:-1}
RELEASE_ROOT=${APPLAUNCH_UPDATE_RELEASE_ROOT:-https://github.com/CardputerZero/launcher/releases/download}
RELEASE_URL=${APPLAUNCH_UPDATE_RELEASE_URL:-$RELEASE_ROOT/launcher-latest}
STATE_DIR=${APPLAUNCH_UPDATE_STATE_DIR:-/var/lib/applaunch-updater}
CACHE_DIR=${APPLAUNCH_UPDATE_CACHE_DIR:-/var/cache/APPLaunch/updates}
STATUS_FILE=$STATE_DIR/status
APP_UID=${APPLAUNCH_UPDATE_UID:-1000}
APP_EXECUTABLE=${APPLAUNCH_UPDATE_EXECUTABLE:-/usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch}
PROC_ROOT=${APPLAUNCH_UPDATE_PROC_ROOT:-/proc}

mkdir -p "$STATE_DIR" "$CACHE_DIR"
chmod 0755 "$STATE_DIR"
chmod 0700 "$CACHE_DIR"
exec 9>"$STATE_DIR/lock"
flock -n 9 || exit 0

tmp_dir=$(mktemp -d "$STATE_DIR/run.XXXXXX")
package=$tmp_dir/applaunch_arm64.deb
checksum=$tmp_dir/applaunch_arm64.deb.sha256
abi_file=$tmp_dir/applaunch_arm64.deb.update-abi
phase=starting
rollback=
cleanup() { rm -rf "$tmp_dir"; [ -z "$SELF_DIR" ] || rm -rf "$SELF_DIR"; }
trap cleanup EXIT
status() {
    printf '%s\n' "$1" >"$STATUS_FILE.tmp"
    chmod 0644 "$STATUS_FILE.tmp"
    mv -f "$STATUS_FILE.tmp" "$STATUS_FILE"
}
fail() { status "failed:$1"; exit 1; }

APP_USER=$(getent passwd "$APP_UID" | cut -d: -f1 || true)
user_systemctl() {
    runuser -u "$APP_USER" -- env \
        XDG_RUNTIME_DIR="/run/user/$APP_UID" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$APP_UID/bus" \
        systemctl --user "$@"
}
recover_service() {
    [ -n "$APP_USER" ] || return 0
    systemctl start "user@$APP_UID.service" >/dev/null 2>&1 || true
    user_systemctl daemon-reload >/dev/null 2>&1 || true
    user_systemctl restart APPLaunch.service >/dev/null 2>&1 || \
        user_systemctl start APPLaunch.service >/dev/null 2>&1 || true
}
service_healthy() {
    [ -n "$APP_USER" ] || return 1
    checks=${APPLAUNCH_UPDATE_HEALTH_CHECKS:-3}
    delay=${APPLAUNCH_UPDATE_HEALTH_DELAY:-1}
    attempt=1
    while [ "$attempt" -le "$checks" ]; do
        user_systemctl is-active --quiet APPLaunch.service || return 1
        main_pid=$(user_systemctl show APPLaunch.service --property=MainPID --value 2>/dev/null || true)
        case "$main_pid" in
            ''|*[!0-9]*|0) return 1 ;;
        esac
        running_executable=$(readlink "$PROC_ROOT/$main_pid/exe" 2>/dev/null || true)
        [ "$running_executable" = "$APP_EXECUTABLE" ] || return 1
        [ "$attempt" -eq "$checks" ] || sleep "$delay"
        attempt=$((attempt + 1))
    done
}
package_healthy() {
    expected_version=$1
    package_status=$(dpkg-query -W -f='${db:Status-Status}' "$PACKAGE_NAME" 2>/dev/null || true)
    active_version=$(dpkg-query -W -f='${Version}' "$PACKAGE_NAME" 2>/dev/null || true)
    [ "$package_status" = installed ] && [ "$active_version" = "$expected_version" ]
}
dpkg_healthy() {
    [ -z "$(dpkg --audit 2>/dev/null || true)" ]
}
rollback_and_fail() {
    reason=$1
    status "recovering:$reason"
    dpkg --configure -a >/dev/null 2>&1 || true
    if ! dpkg -i "$rollback" >/dev/null 2>&1; then
        recover_service
        fail "$reason:rollback-install"
    fi
    dpkg --configure -a >/dev/null 2>&1 || fail "$reason:rollback-configure"
    recover_service
    package_healthy "$installed" || fail "$reason:rollback-package-health"
    dpkg_healthy || fail "$reason:rollback-audit"
    service_healthy || fail "$reason:rollback-service-health"
    fail "$reason"
}
cancel_and_exit() {
    trap - HUP INT TERM
    status "cancelling:$phase"
    case "$phase" in
        repairing)
            dpkg --configure -a >/dev/null 2>&1 || fail cancelled-repair
            ;;
        installing|restarting)
            dpkg --configure -a >/dev/null 2>&1 || true
            if [ -z "$rollback" ] || ! dpkg -i "$rollback" >/dev/null 2>&1; then
                recover_service
                fail cancelled-rollback-install
            fi
            dpkg --configure -a >/dev/null 2>&1 || fail cancelled-rollback-configure
            recover_service
            package_healthy "$installed" || fail cancelled-rollback-package-health
            dpkg_healthy || fail cancelled-rollback-audit
            service_healthy || fail cancelled-rollback-service-health
            ;;
        complete)
            status "succeeded:$candidate"
            exit 0
            ;;
    esac
    status cancelled
    exit 0
}
trap cancel_and_exit HUP INT TERM

previous_status=$(sed -n '1p' "$STATUS_FILE" 2>/dev/null || true)
phase=downloading
status downloading:5
wget -q --https-only --timeout=30 --tries=3 "$RELEASE_URL/applaunch_arm64.deb.update-abi" -O "$abi_file" || fail incompatible
[ "$(tr -d '[:space:]' <"$abi_file")" = "$UPDATE_ABI" ] || fail incompatible
status downloading:20
wget -q --https-only --timeout=30 --tries=3 "$RELEASE_URL/applaunch_arm64.deb" -O "$package" || fail download-package
status downloading:65
wget -q --https-only --timeout=30 --tries=3 "$RELEASE_URL/applaunch_arm64.deb.sha256" -O "$checksum" || fail download-checksum
status downloading:70
expected=$(awk 'NF && $1 ~ /^[0-9a-fA-F]{64}$/ { print tolower($1); exit }' "$checksum")
[ ${#expected} -eq 64 ] || fail checksum-manifest
actual=$(sha256sum "$package" | awk '{print $1}')
[ "$actual" = "$expected" ] || fail checksum
status downloading:75

[ "$(dpkg-deb -f "$package" Package)" = "$PACKAGE_NAME" ] || fail package-name
[ "$(dpkg-deb -f "$package" Architecture)" = "$ARCHITECTURE" ] || fail architecture
[ "$(dpkg-deb -f "$package" X-CardputerZero-Update-ABI 2>/dev/null || true)" = "$UPDATE_ABI" ] || fail incompatible
candidate=$(dpkg-deb -f "$package" Version)
installed=$(dpkg-query -W -f='${Version}' "$PACKAGE_NAME" 2>/dev/null) || fail installed-version
audit=$(dpkg --audit 2>/dev/null || true)
if [ -n "$audit" ]; then
    phase=repairing
    status repairing:80
    dpkg --configure -a >/dev/null 2>&1 || fail repair
    installed=$(dpkg-query -W -f='${Version}' "$PACKAGE_NAME" 2>/dev/null) || fail installed-version
fi
if [ "$candidate" = "$installed" ]; then
    case "$previous_status" in
        installing|installing:*|repairing|repairing:*|recovering:*)
            package_healthy "$candidate" || fail interrupted-package-health
            recover_service
            service_healthy || fail interrupted-service-health
            install -m 0600 "$package" "$CACHE_DIR/installed.deb"
            status "succeeded:$candidate"
            exit 0
            ;;
    esac
fi
dpkg --compare-versions "$candidate" gt "$installed" || fail version-not-newer

# Retain the last trusted package so a later upgrade can roll back after a
# failed install or service health check.
if [ -f "$CACHE_DIR/installed.deb" ] &&
   [ "$(dpkg-deb -f "$CACHE_DIR/installed.deb" Package 2>/dev/null || true)" = "$PACKAGE_NAME" ] &&
   [ "$(dpkg-deb -f "$CACHE_DIR/installed.deb" Version 2>/dev/null || true)" = "$installed" ]; then
    cp "$CACHE_DIR/installed.deb" "$tmp_dir/rollback.deb"
    rollback=$tmp_dir/rollback.deb
fi
if [ -z "$rollback" ]; then
    upstream=${installed%%-*}
    old_name="${PACKAGE_NAME}_${installed}_${ARCHITECTURE}.deb"
    old_url="$RELEASE_ROOT/launcher-v$upstream/$old_name"
    old_checksum_url="$RELEASE_ROOT/launcher-v$upstream/applaunch_arm64.deb.sha256"
    if wget -q --https-only --timeout=30 --tries=3 "$old_url" -O "$tmp_dir/rollback.deb" &&
       wget -q --https-only --timeout=30 --tries=3 "$old_checksum_url" -O "$tmp_dir/rollback.sha256"; then
        rollback_expected=$(awk 'NF && $1 ~ /^[0-9a-fA-F]{64}$/ { print tolower($1); exit }' "$tmp_dir/rollback.sha256")
        rollback_actual=$(sha256sum "$tmp_dir/rollback.deb" | awk '{print $1}')
        if [ ${#rollback_expected} -eq 64 ] && [ "$rollback_actual" = "$rollback_expected" ] &&
           [ "$(dpkg-deb -f "$tmp_dir/rollback.deb" Package 2>/dev/null || true)" = "$PACKAGE_NAME" ] &&
           [ "$(dpkg-deb -f "$tmp_dir/rollback.deb" Architecture 2>/dev/null || true)" = "$ARCHITECTURE" ] &&
           [ "$(dpkg-deb -f "$tmp_dir/rollback.deb" Version 2>/dev/null || true)" = "$installed" ]; then
            rollback=$tmp_dir/rollback.deb
        fi
    fi
fi
[ -n "$rollback" ] || fail rollback-unavailable

phase=installing
status installing:85
if ! dpkg -i "$package"; then
    rollback_and_fail install
fi

package_healthy "$candidate" || rollback_and_fail package-health

phase=restarting
status restarting:95
recover_service
service_healthy || rollback_and_fail service-health
dpkg_healthy || rollback_and_fail dpkg-audit

install -m 0600 "$package" "$CACHE_DIR/installed.deb"
phase=complete
status "succeeded:$candidate"
"""


def _service_text(config: PackageConfig) -> str:
    if config.service_scope == "system":
        return f"""[Unit]
Description={config.app_name} Service
After=systemd-user-sessions.service

[Service]
ExecStart=/{_posix_path(config.bin_path / config.bin_name)}
WorkingDirectory=/{_posix_path(config.install_prefix)}
Restart={config.service_restart}
RestartSec=1
StartLimitInterval=0

[Install]
WantedBy=multi-user.target
"""
    return f"""[Unit]
Description={config.app_name} Service
After=pipewire-pulse.service
Wants=pipewire-pulse.service

[Service]
ExecStartPre=-/usr/bin/xdg-user-dirs-update
ExecStart=/{_posix_path(config.bin_path / config.bin_name)}
WorkingDirectory=/{_posix_path(config.install_prefix)}
Restart={config.service_restart}
RestartSec=1
StartLimitInterval=0

[Install]
WantedBy=default.target
"""


def _write_text(path: Path, text: str, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    _chmod(path, mode)


def _repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _default_project_dir(repo_root: Path, project: str) -> Path:
    candidate = repo_root / "projects" / project
    if candidate.is_dir():
        return candidate
    return _resolve_path(project, repo_root)


def _prepare_paths(
    src_folder: str | os.PathLike[str],
    output_dir: str | os.PathLike[str] | None,
    work_dir: str | os.PathLike[str] | None,
    config: PackageConfig,
    project: str = DEFAULT_PROJECT,
    project_dir: str | os.PathLike[str] | None = None,
) -> Paths:
    repo_root = _repo_root()
    tool_dir = Path(__file__).resolve().parent
    resolved_project_dir = (
        _resolve_path(project_dir, repo_root)
        if project_dir
        else _default_project_dir(repo_root, project)
    )
    src_dir = _resolve_path(src_folder, resolved_project_dir)
    out_dir = _resolve_path(output_dir or resolved_project_dir / "tools", repo_root)
    staging_parent = _resolve_path(work_dir or out_dir, repo_root)
    package_root = staging_parent / f"debian-{config.app_name}"
    return Paths(
        repo_root=repo_root,
        tool_dir=tool_dir,
        project_dir=resolved_project_dir,
        src_dir=src_dir,
        output_dir=out_dir,
        work_dir=staging_parent,
        package_root=package_root,
        package_file=out_dir / config.file_name,
    )


def prepare_package_tree(
    config: PackageConfig,
    paths: Paths,
    app_tree: str | os.PathLike[str] | None = None,
) -> None:
    if paths.package_root.exists():
        shutil.rmtree(paths.package_root)

    for directory in (
        PurePosixPath("DEBIAN"),
        config.bin_path,
        config.service_path,
    ):
        _mkdir(paths.package_root, directory)

    app_src = (
        _resolve_path(app_tree, paths.project_dir)
        if app_tree
        else _default_app_tree(paths.src_dir, config.app_name)
    )
    app_dst = paths.package_root / Path(*config.install_prefix.parts)
    _copy_tree(app_src, app_dst)

    binary = _find_binary(paths.src_dir, config.bin_name)
    _copy_file(binary, paths.package_root / Path(*config.bin_path.parts) / config.bin_name, mode=0o755)

    copied_bins = _copy_optional_binaries(paths.src_dir, paths.package_root, config)
    _write_text(paths.package_root / "DEBIAN" / "control", _control_text(config))
    _write_text(paths.package_root / "DEBIAN" / "postinst", _postinst_text(config), mode=0o755)
    _write_text(paths.package_root / "DEBIAN" / "prerm", _prerm_text(config), mode=0o755)
    _write_text(
        paths.package_root / Path(*config.service_path.parts) / f"{config.app_name}.service",
        _service_text(config),
    )
    if config.app_name == APP_NAME and config.package_name == PACKAGE_NAME:
        _write_text(
            paths.package_root / Path(*SYSTEM_SERVICE_PATH.parts) / "applaunch-updater.service",
            _updater_service_text(),
        )
        _write_text(
            paths.package_root / Path(*SYSTEM_SERVICE_PATH.parts) / "applaunch-apt-update.service",
            _apt_update_service_text(),
        )
        _write_text(
            paths.package_root / Path(*POLKIT_RULES_PATH.parts) / "60-applaunch-updater.rules",
            _updater_polkit_text(),
        )
        _write_text(
            paths.package_root / Path(*LIBEXEC_PATH.parts) / "applaunch-updater",
            _updater_script_text(), mode=0o755,
        )

    print(f"Staged package tree: {paths.package_root}")
    print(f"  binary: {binary}")
    print(f"  app tree: {app_src}")
    if copied_bins:
        print(f"  optional binaries: {', '.join(copied_bins)}")


def _tar_filter(tar_info: tarfile.TarInfo) -> tarfile.TarInfo:
    tar_info.uid = 0
    tar_info.gid = 0
    tar_info.uname = "root"
    tar_info.gname = "root"
    tar_info.mtime = _source_date_epoch()
    if tar_info.isdir():
        tar_info.mode = 0o755
    elif tar_info.mode & 0o111:
        tar_info.mode = 0o755
    else:
        tar_info.mode = 0o644
    return tar_info


def _tar_tree(root: Path, names: Iterable[str]) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz", format=tarfile.GNU_FORMAT) as tar:
        for name in names:
            source = root / name
            if not source.exists():
                continue
            tar.add(source, arcname=name, recursive=True, filter=_tar_filter)
    return buffer.getvalue()


def _data_members(package_root: Path) -> list[str]:
    return sorted(
        entry.name for entry in package_root.iterdir() if entry.name != "DEBIAN"
    )


def _ar_member_header(name: str, size: int, mode: int = 0o100644) -> bytes:
    if len(name) > 15:
        raise PackError(f"ar member name too long: {name}")
    header = (
        f"{name + '/':<16}"
        f"{_source_date_epoch():<12}"
        f"{0:<6}"
        f"{0:<6}"
        f"{format(mode, 'o'):<8}"
        f"{size:<10}`\n"
    )
    return header.encode("ascii")


def _write_ar_member(handle, name: str, data: bytes) -> None:
    handle.write(_ar_member_header(name, len(data)))
    handle.write(data)
    if len(data) % 2:
        handle.write(b"\n")


def build_deb_with_python(package_root: Path, deb_file: Path) -> None:
    control_tar = _tar_tree(package_root / "DEBIAN", ("control", "postinst", "prerm"))
    data_tar = _tar_tree(package_root, _data_members(package_root))

    deb_file.parent.mkdir(parents=True, exist_ok=True)
    with deb_file.open("wb") as handle:
        handle.write(b"!<arch>\n")
        _write_ar_member(handle, "debian-binary", b"2.0\n")
        _write_ar_member(handle, "control.tar.gz", control_tar)
        _write_ar_member(handle, "data.tar.gz", data_tar)


def build_deb_with_dpkg(package_root: Path, deb_file: Path) -> None:
    deb_file.parent.mkdir(parents=True, exist_ok=True)
    command = ["dpkg-deb", "--root-owner-group", "-b", str(package_root), str(deb_file)]
    subprocess.run(command, check=True)


def build_deb(package_root: Path, deb_file: Path, builder: str = "auto") -> str:
    selected = builder
    if builder == "auto":
        selected = "dpkg-deb" if shutil.which("dpkg-deb") else "python"

    if selected == "dpkg-deb":
        build_deb_with_dpkg(package_root, deb_file)
    elif selected == "python":
        build_deb_with_python(package_root, deb_file)
    else:
        raise PackError(f"unsupported builder: {builder}")
    return selected


def create_deb_package(
    version: str = DEFAULT_VERSION,
    src_folder: str | os.PathLike[str] = "dist",
    revision: str = DEFAULT_REVISION,
    architecture: str = DEFAULT_ARCHITECTURE,
    output_dir: str | os.PathLike[str] | None = None,
    work_dir: str | os.PathLike[str] | None = None,
    builder: str = "auto",
    app_tree: str | os.PathLike[str] | None = None,
    keep_staging: bool = True,
    project: str = DEFAULT_PROJECT,
    project_dir: str | os.PathLike[str] | None = None,
    package_name: str = PACKAGE_NAME,
    app_name: str = APP_NAME,
    bin_name: str = BIN_NAME,
    service_scope: str = "user",
    service_restart: str = "always",
) -> str:
    """Build a project as a Debian package and return the output path."""
    config = PackageConfig(
        version=version,
        revision=revision,
        architecture=architecture,
        package_name=package_name,
        app_name=app_name,
        bin_name=bin_name,
        section=app_name,
        description=f"M5CardputerZero {app_name}",
        service_scope=service_scope,
        service_restart=service_restart,
    )
    paths = _prepare_paths(src_folder, output_dir, work_dir, config, project, project_dir)

    print(f"Creating Debian package {config.file_name} ...")
    prepare_package_tree(config, paths, app_tree=app_tree)
    selected_builder = build_deb(paths.package_root, paths.package_file, builder=builder)

    if not keep_staging:
        shutil.rmtree(paths.package_root)

    print(f"Debian package created: {paths.package_file}")
    print(f"Builder: {selected_builder}")
    return str(paths.package_file)


def create_applaunch_deb(**kwargs) -> str:
    """Compatibility wrapper for older callers that imported this helper."""
    return create_deb_package(**kwargs)


def clean_outputs(output_dir: Path, app_name: str, distclean: bool = False) -> None:
    patterns = ["*.deb", f"debian-{app_name}"]
    if distclean:
        patterns.append("m5stack_*")
    for pattern in patterns:
        for path in output_dir.glob(pattern):
            _safe_remove(path)
            print(f"removed: {path}")


def resolve_output_dir(
    project: str,
    project_dir: str | os.PathLike[str] | None,
    output_dir: str | os.PathLike[str] | None,
) -> Path:
    repo_root = _repo_root()
    resolved_project_dir = (
        _resolve_path(project_dir, repo_root)
        if project_dir
        else _default_project_dir(repo_root, project)
    )
    return _resolve_path(output_dir or resolved_project_dir / "tools", repo_root)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package repository projects into Debian .deb files on Linux, macOS, or Windows."
    )
    subparsers = parser.add_subparsers(dest="command")

    build = subparsers.add_parser("build", help="build the Debian package")
    build.add_argument("--project", default=DEFAULT_PROJECT, help="project name under projects/ or a project path")
    build.add_argument("--project-dir", default=None, help="explicit project directory; overrides --project")
    build.add_argument("--package-name", default=PACKAGE_NAME, help="Debian package name")
    build.add_argument("--app-name", default=APP_NAME, help="installed application name under /usr/share")
    build.add_argument("--bin-name", default=BIN_NAME, help="main executable name")
    build.add_argument("--version", default=DEFAULT_VERSION, help="package version")
    build.add_argument("--revision", default=DEFAULT_REVISION, help="Debian package revision")
    build.add_argument("--architecture", default=DEFAULT_ARCHITECTURE, help="Debian architecture")
    build.add_argument("--src", "--src-folder", dest="src", default="dist", help="dist directory containing the built binary; relative paths are resolved from the project directory")
    build.add_argument("--app-tree", default=None, help="resource tree to install as /usr/share/<app-name>")
    build.add_argument(
        "--service-scope",
        choices=("user", "system"),
        default="user",
        help="install a systemd user service or root system service",
    )
    build.add_argument(
        "--service-restart",
        default="always",
        help="systemd Restart policy for the generated service",
    )
    build.add_argument("--output-dir", default=None, help="directory for the generated .deb")
    build.add_argument("--work-dir", default=None, help="directory for the staging tree")
    build.add_argument(
        "--builder",
        choices=("auto", "python", "dpkg-deb"),
        default="auto",
        help="package writer to use; auto prefers dpkg-deb when available",
    )
    build.add_argument("--remove-staging", action="store_true", help="delete staging tree after build")

    def add_clean_args(subparser: argparse.ArgumentParser) -> None:
        subparser.add_argument("--project", default=DEFAULT_PROJECT, help="project name under projects/ or a project path")
        subparser.add_argument("--project-dir", default=None, help="explicit project directory; overrides --project")
        subparser.add_argument("--app-name", default=APP_NAME, help="installed application name under /usr/share")
        subparser.add_argument("--output-dir", default=None, help="directory containing generated package artifacts")

    clean = subparsers.add_parser("clean", help="remove generated .deb files and staging tree")
    add_clean_args(clean)
    distclean = subparsers.add_parser("distclean", help="clean plus legacy m5stack_* outputs")
    add_clean_args(distclean)

    # Backward compatibility: bare execution still builds the APPLaunch package.
    normalized = list(argv)
    if not normalized:
        normalized = ["build"]
    elif normalized[0].startswith("-") and normalized[0] not in ("-h", "--help"):
        normalized.insert(0, "build")
    return parser.parse_args(normalized)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if args.command == "clean":
            clean_outputs(
                resolve_output_dir(args.project, args.project_dir, args.output_dir),
                args.app_name,
            )
            return 0
        if args.command == "distclean":
            clean_outputs(
                resolve_output_dir(args.project, args.project_dir, args.output_dir),
                args.app_name,
                distclean=True,
            )
            return 0

        create_deb_package(
            version=args.version,
            src_folder=args.src,
            revision=args.revision,
            architecture=args.architecture,
            output_dir=args.output_dir,
            work_dir=args.work_dir,
            builder=args.builder,
            app_tree=args.app_tree,
            keep_staging=not args.remove_staging,
            project=args.project,
            project_dir=args.project_dir,
            package_name=args.package_name,
            app_name=args.app_name,
            bin_name=args.bin_name,
            service_scope=args.service_scope,
            service_restart=args.service_restart,
        )
        return 0
    except (OSError, subprocess.CalledProcessError, PackError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
