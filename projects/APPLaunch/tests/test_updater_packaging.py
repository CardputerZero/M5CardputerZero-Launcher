#!/usr/bin/env python3
import hashlib
import importlib.util
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

repo = Path(__file__).resolve().parents[3]
module_path = repo / "scripts" / "debian_packager.py"
spec = importlib.util.spec_from_file_location("debian_packager_updater", module_path)
packager = importlib.util.module_from_spec(spec)
assert spec.loader
sys.modules[spec.name] = packager
spec.loader.exec_module(packager)


def executable(path: Path, text: str) -> None:
    path.write_text("#!/bin/sh\nset -eu\n" + text, encoding="utf-8")
    path.chmod(0o755)


def ar_members(data: bytes) -> dict[str, bytes]:
    assert data.startswith(b"!<arch>\n")
    members: dict[str, bytes] = {}
    offset = 8
    while offset < len(data):
        header = data[offset:offset + 60]
        assert len(header) == 60 and header[58:60] == b"`\n"
        name = header[:16].decode("ascii").rstrip().removesuffix("/")
        size = int(header[48:58])
        offset += 60
        members[name] = data[offset:offset + size]
        offset += size + (size % 2)
    return members


def run_update(fail_install: bool = False,
               provide_rollback: bool = True,
               fail_rollback: bool = False,
               fail_configure: bool = False,
               keep_audit_dirty: bool = False,
               unhealthy_process: bool = False,
               audit_required: bool = False,
               interrupted_candidate: bool = False,
               compatible: bool = True,
               cancel_during_download: bool = False,
               cancel_during_install: bool = False) -> tuple[subprocess.CompletedProcess[str], str, str]:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        fake_bin = root / "bin"
        release = root / "release"
        release_root = root / "releases"
        old_release = release_root / "launcher-v1.0"
        state = root / "state"
        cache = root / "cache"
        proc = root / "proc"
        fake_bin.mkdir()
        release.mkdir()
        old_release.mkdir(parents=True)
        (proc / "4242").mkdir(parents=True)
        app_executable = root / "M5CardputerZero-APPLaunch"
        app_executable.write_text("current executable", encoding="ascii")
        running_executable = root / "old-deleted-executable" if unhealthy_process else app_executable
        (proc / "4242" / "exe").symlink_to(running_executable)
        if interrupted_candidate:
            state.mkdir()
            (state / "status").write_text("installing:85\n", encoding="ascii")
        package = release / "applaunch_arm64.deb"
        package.write_bytes(b"trusted test package")
        digest = hashlib.sha256(package.read_bytes()).hexdigest()
        (release / "applaunch_arm64.deb.sha256").write_text(
            f"{digest}  applaunch_arm64.deb\n", encoding="ascii"
        )
        (release / "applaunch_arm64.deb.update-abi").write_text(
            "1\n" if compatible else "0\n", encoding="ascii"
        )
        if provide_rollback:
            old_package = old_release / "applaunch_1.0_arm64.deb"
            old_package.write_bytes(b"trusted previous package")
            old_digest = hashlib.sha256(old_package.read_bytes()).hexdigest()
            (old_release / "applaunch_arm64.deb.sha256").write_text(
                f"{old_digest}  applaunch_arm64.deb\n", encoding="ascii"
            )
        log = root / "commands.log"

        executable(
            fake_bin / "wget",
            'src=$5\ndst=$7\n'
            'case "$src" in */applaunch_arm64.deb) '
            '[ "${TEST_CANCEL_DOWNLOAD:-0}" != 1 ] || { kill -TERM "$PPID"; exit 143; };; esac\n'
            'cp "${src#file://}" "$dst"\n',
        )
        executable(
            fake_bin / "dpkg-deb",
            'case "$3" in Package) echo applaunch;; Architecture) echo arm64;; '
            'X-CardputerZero-Update-ABI) echo "${TEST_UPDATE_ABI:-1}";; '
            'Version) case "$2" in *installed.deb|*rollback.deb) echo 1.0;; *) echo 2.0;; esac;; esac\n',
        )
        executable(
            fake_bin / "dpkg-query",
            'case "$2" in *Status*) echo installed;; *) '
            'if [ -f "$TEST_MARKER" ]; then echo 2.0; else echo 1.0; fi;; esac\n',
        )
        executable(
            fake_bin / "dpkg",
            'echo "dpkg $*" >>"$TEST_LOG"\n'
            'case "$1" in --compare-versions) exit 0;; --audit) '
            '[ ! -f "$TEST_AUDIT_MARKER" ] || echo "package needs configuration";; --configure) '
            '[ "${TEST_FAIL_CONFIGURE:-0}" != 1 ] || exit 44; '
            '[ "${TEST_KEEP_AUDIT_DIRTY:-0}" = 1 ] || rm -f "$TEST_AUDIT_MARKER";; -i) '
            'case "$2" in *rollback.deb) [ "${TEST_FAIL_ROLLBACK:-0}" != 1 ] || exit 43; '
            'rm -f "$TEST_MARKER";; *) [ "${TEST_FAIL_INSTALL:-0}" != 1 ] || exit 42; '
            '[ "${TEST_CANCEL_INSTALL:-0}" != 1 ] || { kill -TERM "$PPID"; exit 143; }; '
            'touch "$TEST_MARKER";; esac;; esac\n',
        )
        executable(fake_bin / "systemctl", 'echo "systemctl $*" >>"$TEST_LOG"\n')
        executable(
            fake_bin / "runuser",
            'echo "runuser $*" >>"$TEST_LOG"\ncase "$*" in *is-active*) exit 0;; '
            '*property=MainPID*) echo 4242;; esac\n',
        )
        executable(fake_bin / "getent", 'echo "tester:x:1000:1000::/tmp:/bin/sh"\n')

        env = os.environ.copy()
        env.update(
            {
                "PATH": f"{fake_bin}:{env['PATH']}",
                "APPLAUNCH_UPDATE_RELEASE_URL": f"file://{release}",
                "APPLAUNCH_UPDATE_RELEASE_ROOT": f"file://{release_root}",
                "APPLAUNCH_UPDATE_STATE_DIR": str(state),
                "APPLAUNCH_UPDATE_CACHE_DIR": str(cache),
                "TEST_LOG": str(log),
                "TEST_MARKER": str(root / "installed.marker"),
                "TEST_FAIL_INSTALL": "1" if fail_install else "0",
                "TEST_FAIL_ROLLBACK": "1" if fail_rollback else "0",
                "TEST_FAIL_CONFIGURE": "1" if fail_configure else "0",
                "TEST_KEEP_AUDIT_DIRTY": "1" if keep_audit_dirty else "0",
                "TEST_AUDIT_MARKER": str(root / "audit.marker"),
                "APPLAUNCH_UPDATER_REEXEC": "1",
                "APPLAUNCH_UPDATE_EXECUTABLE": str(app_executable),
                "APPLAUNCH_UPDATE_PROC_ROOT": str(proc),
                "APPLAUNCH_UPDATE_HEALTH_CHECKS": "1",
                "TEST_UPDATE_ABI": "1" if compatible else "0",
                "TEST_CANCEL_DOWNLOAD": "1" if cancel_during_download else "0",
                "TEST_CANCEL_INSTALL": "1" if cancel_during_install else "0",
            }
        )
        if interrupted_candidate:
            Path(env["TEST_MARKER"]).touch()
        if audit_required:
            Path(env["TEST_AUDIT_MARKER"]).touch()
        result = subprocess.run(
            ["sh"], input=packager._updater_script_text(), text=True,
            env=env, capture_output=True, check=False,
        )
        status = (state / "status").read_text(encoding="utf-8").strip()
        commands = log.read_text(encoding="utf-8") if log.exists() else ""
        return result, status, commands


success, status, commands = run_update()
assert success.returncode == 0, (success.stderr, success.stdout, status, commands)
assert status == "succeeded:2.0"
assert "dpkg -i" in commands
assert "is-active --quiet APPLaunch.service" in commands
assert "property=MainPID" in commands

failure, status, commands = run_update(fail_install=True)
assert failure.returncode != 0
assert status == "failed:install"
assert "dpkg --configure -a" in commands

interrupted, status, commands = run_update(
    audit_required=True, interrupted_candidate=True
)
assert interrupted.returncode == 0
assert status == "succeeded:2.0"
assert "dpkg -i" not in commands
assert "restart APPLaunch.service" in commands
assert "restart APPLaunch.service" in commands

rollback_failure, status, commands = run_update(fail_install=True, fail_rollback=True)
assert rollback_failure.returncode != 0
assert status == "failed:install:rollback-install"

configure_failure, status, commands = run_update(
    fail_install=True, fail_configure=True
)
assert configure_failure.returncode != 0
assert status == "failed:install:rollback-configure"

audit_failure, status, commands = run_update(
    fail_install=True, audit_required=True, keep_audit_dirty=True
)
assert audit_failure.returncode != 0
assert status == "failed:install:rollback-audit"

process_failure, status, commands = run_update(unhealthy_process=True)
assert process_failure.returncode != 0
assert status == "failed:service-health:rollback-service-health"

repaired, status, commands = run_update(audit_required=True)
assert repaired.returncode == 0
assert status == "succeeded:2.0"
assert "dpkg --audit" in commands
assert "dpkg --configure -a" in commands

missing_rollback, status, commands = run_update(provide_rollback=False)
assert missing_rollback.returncode != 0
assert status == "failed:rollback-unavailable"

incompatible, status, commands = run_update(compatible=False)
assert incompatible.returncode != 0
assert status == "failed:incompatible"
assert "dpkg -i" not in commands
assert "applaunch_arm64.deb" not in commands

cancelled_download, status, commands = run_update(cancel_during_download=True)
assert cancelled_download.returncode == 0
assert status == "cancelled"
assert "dpkg -i" not in commands

cancelled_install, status, commands = run_update(cancel_during_install=True)
assert cancelled_install.returncode == 0
assert status == "cancelled"
assert "dpkg -i" in commands
assert "rollback.deb" in commands

old_source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH")
try:
    os.environ["SOURCE_DATE_EPOCH"] = "1704067200"
    control = packager._control_text(packager.PackageConfig())
    assert "Packaged-Date: 2024-01-01 00:00:00 UTC\n" in control
    assert "X-CardputerZero-Update-ABI: 1\n" in control
    other_control = packager._control_text(
        packager.PackageConfig(app_name="OtherApp", package_name="other")
    )
    assert "X-CardputerZero-Update-ABI" not in other_control
    assert packager._ar_member_header("data.tar.gz", 1)[16:28].strip() == b"1704067200"

    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        package_root = root / "package"
        control_root = package_root / "DEBIAN"
        data_root = package_root / "usr" / "share" / "APPLaunch"
        control_root.mkdir(parents=True)
        data_root.mkdir(parents=True)
        (control_root / "control").write_text(control, encoding="utf-8")
        (control_root / "postinst").write_text("#!/bin/sh\n", encoding="ascii")
        (control_root / "prerm").write_text("#!/bin/sh\n", encoding="ascii")
        (data_root / "payload.txt").write_text("deterministic payload\n", encoding="ascii")

        first_deb = root / "first.deb"
        second_deb = root / "second.deb"
        packager.build_deb_with_python(package_root, first_deb)
        time.sleep(1.1)
        packager.build_deb_with_python(package_root, second_deb)

        first_bytes = first_deb.read_bytes()
        assert first_bytes == second_deb.read_bytes()
        members = ar_members(first_bytes)
        for name in ("control.tar.gz", "data.tar.gz"):
            compressed_tar = members[name]
            assert compressed_tar[:4] == b"\x1f\x8b\x08\x00"
            assert int.from_bytes(compressed_tar[4:8], "little") == 1704067200
finally:
    if old_source_date_epoch is None:
        os.environ.pop("SOURCE_DATE_EPOCH", None)
    else:
        os.environ["SOURCE_DATE_EPOCH"] = old_source_date_epoch

assert "/usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch" in packager._updater_script_text()
assert 'mktemp -d "$state_root/self.XXXXXX"' in packager._updater_script_text()
assert "/run/applaunch-updater" not in packager._updater_script_text()
assert "TimeoutStartSec=20min" in packager._updater_service_text()
assert "TimeoutStopSec=5min" in packager._updater_service_text()
assert 'action.lookup("unit") == "applaunch-updater.service"' in packager._updater_polkit_text()
assert 'action.lookup("verb") == "stop"' in packager._updater_polkit_text()
assert "status downloading:5" in packager._updater_script_text()
assert "status installing:85" in packager._updater_script_text()
assert "status restarting:95" in packager._updater_script_text()

workflow = (repo / ".github" / "workflows" / "launcher-build.yml").read_text(
    encoding="utf-8"
)
assert "branches: [master, ci/**]" in workflow
assert "if: github.ref == 'refs/heads/master'" in workflow
assert "startsWith(github.ref, 'refs/heads/ci/')" not in workflow
assert workflow.count("tag_name: launcher-latest") == 1
assert "scripts/build_cardputerzero_release_local.sh" in workflow
assert "python3 scripts/debian_packager.py --version" not in workflow
assert "artifacts/applaunch_arm64.deb.update-info" in workflow
assert "${{ steps.version.outputs.version }}" in workflow
assert "${GITHUB_SHA:0:12}" in workflow

release_builder = (repo / "scripts" / "build_cardputerzero_release_local.sh").read_text(
    encoding="utf-8"
)
assert 'CLEAN_BUILD=${CLEAN_BUILD:-1}' in release_builder
assert '"$ROOT/projects/$project/main/build"' in release_builder
assert "applaunch_arm64.deb.update-info" in release_builder
assert "printf 'format=1\\n'" in release_builder
assert "printf 'commit=%s\\n'" in release_builder
