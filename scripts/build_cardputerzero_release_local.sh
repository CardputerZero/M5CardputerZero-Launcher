#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 <debian-version> [revision]" >&2
    echo "example: $0 0.6.29+local.audiofix5 1" >&2
    exit 2
fi

VERSION=$1
REVISION=${2:-1}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
JOBS=${JOBS:-$(nproc)}
OUTPUT_DIR=${OUTPUT_DIR:-"$ROOT/../build-artifacts/launcher-release"}
SCONS=${SCONS:-scons}

for command in aarch64-linux-gnu-g++ dpkg-deb python3; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "ERROR: required command not found: $command" >&2
        exit 1
    fi
done
if ! command -v "$SCONS" >/dev/null 2>&1 && [ ! -x "$SCONS" ]; then
    echo "ERROR: SCons not found: $SCONS" >&2
    echo "Set SCONS to the venv executable, for example:" >&2
    echo "  SCONS=/home/m5stack/.venvs/cardputerzero-build/bin/scons" >&2
    exit 1
fi

for project in APPLaunch AppStore Calculator LaunchWizard ZClaw; do
    if [ ! -f "$ROOT/projects/$project/SConstruct" ]; then
        echo "ERROR: missing project or submodule: projects/$project/SConstruct" >&2
        exit 1
    fi
done

export CardputerZero=y
export CONFIG_REPO_AUTOMATION=y
export APPLAUNCH_VERSION=${APPLAUNCH_VERSION:-${VERSION%%+*}}
export APPLAUNCH_CHANNEL=${APPLAUNCH_CHANNEL:-local}
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" show -s --format=%ct HEAD)}

build_project() {
    project=$1
    echo "=== Building $project for CardputerZero ARM64 ==="
    (
        cd "$ROOT/projects/$project"
        "$SCONS" -j"$JOBS" --implicit-deps-changed
    )
}

build_project APPLaunch
build_project AppStore
build_project Calculator
build_project LaunchWizard
build_project ZClaw

echo "=== Aggregating release payload ==="
install -d "$ROOT/projects/APPLaunch/dist/bin"
install -m 0755 "$ROOT/projects/AppStore/dist/M5CardputerZero-AppStore" \
    "$ROOT/projects/APPLaunch/dist/bin/"
install -m 0755 "$ROOT/projects/Calculator/dist/M5CardputerZero-Calculator" \
    "$ROOT/projects/APPLaunch/dist/bin/"
install -m 0755 "$ROOT/projects/ZClaw/dist/ZClaw" \
    "$ROOT/projects/APPLaunch/dist/bin/"

install -d "$ROOT/projects/APPLaunch/dist/APPLaunch"
cp -a "$ROOT/projects/AppStore/dist/APPLaunch/." \
    "$ROOT/projects/APPLaunch/dist/APPLaunch/"
cp -a "$ROOT/projects/ZClaw/dist/APPLaunch/." \
    "$ROOT/projects/APPLaunch/dist/APPLaunch/"
install -D -m 0755 "$ROOT/projects/LaunchWizard/dist/LaunchWizard" \
    "$ROOT/projects/APPLaunch/dist/APPLaunch/bin/LaunchWizard"

install -d "$OUTPUT_DIR"
python3 "$ROOT/scripts/debian_packager.py" build \
    --project APPLaunch \
    --package-name applaunch \
    --app-name APPLaunch \
    --bin-name M5CardputerZero-APPLaunch \
    --version "$VERSION" \
    --revision "$REVISION" \
    --architecture arm64 \
    --output-dir "$OUTPUT_DIR"

PACKAGE="$OUTPUT_DIR/applaunch_${VERSION}-${REVISION}_arm64.deb"
test -f "$PACKAGE"
test "$(dpkg-deb -f "$PACKAGE" Architecture)" = arm64
CONTENTS=$(mktemp)
trap 'rm -f "$CONTENTS"' EXIT
dpkg-deb -c "$PACKAGE" >"$CONTENTS"
grep -q './usr/share/APPLaunch/bin/LaunchWizard$' "$CONTENTS"
grep -q './usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch$' "$CONTENTS"

echo "=== Release package ==="
dpkg-deb -f "$PACKAGE" Package Version Architecture
stat -c '%s %n' "$PACKAGE"
sha256sum "$PACKAGE"
