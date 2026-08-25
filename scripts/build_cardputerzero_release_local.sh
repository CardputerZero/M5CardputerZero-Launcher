#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -gt 2 ]; then
    echo "usage: $0 [debian-version] [revision]" >&2
    echo "example: $0 0.6.31+local.test1 m5stack1" >&2
    exit 2
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

derive_version() {
    tag=$(git -C "$ROOT" describe --tags --match 'launcher-v[0-9]*' \
        --abbrev=0 2>/dev/null || true)
    base=${tag#launcher-v}
    [ -n "$base" ] || base=0.0.0
    head=$(git -C "$ROOT" rev-parse --short=12 HEAD)
    if [ -z "$(git -C "$ROOT" status --porcelain=v1)" ]; then
        if [ -n "$tag" ] && [ "$(git -C "$ROOT" rev-list --count "$tag"..HEAD)" -eq 0 ]; then
            printf '%s\n' "$base"
        else
            printf '%s+git.%s\n' "$base" "$head"
        fi
        return
    fi

    fingerprint=$(
        {
            git -C "$ROOT" diff --binary HEAD
            git -C "$ROOT" ls-files --others --exclude-standard -z |
                LC_ALL=C sort -z |
                while IFS= read -r -d '' file; do
                    sha256sum -- "$ROOT/$file"
                done
        } | sha256sum | cut -c1-12
    )
    printf '%s+local.%s.w%s\n' "$base" "$head" "$fingerprint"
}

VERSION=${1:-$(derive_version)}
REVISION=${2:-m5stack1}
JOBS=${JOBS:-$(nproc)}
OUTPUT_DIR=${OUTPUT_DIR:-"$ROOT/../build-artifacts/launcher-release"}
SCONS=${SCONS:-scons}
CLEAN_BUILD=${CLEAN_BUILD:-1}

case "$CLEAN_BUILD" in
    0|1) ;;
    *) echo "ERROR: CLEAN_BUILD must be 0 or 1" >&2; exit 2 ;;
esac

for command in aarch64-linux-gnu-gcc aarch64-linux-gnu-g++ dpkg-deb python3; do
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
    case "$project" in
        APPLaunch|AppStore|Calculator|LaunchWizard|ZClaw) ;;
        *) echo "ERROR: unsupported project: $project" >&2; return 2 ;;
    esac
    if [ "$CLEAN_BUILD" = 1 ]; then
        echo "=== Cleaning $project build outputs ==="
        rm -rf -- \
            "$ROOT/projects/$project/build" \
            "$ROOT/projects/$project/dist" \
            "$ROOT/projects/$project/main/build"
    fi
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
aarch64-linux-gnu-gcc -std=c11 -Os -s -Wall -Wextra -Werror \
    "$ROOT/scripts/cardputer_adb_hotplug.c" \
    -o "$ROOT/projects/APPLaunch/dist/APPLaunch/adb/cardputer-adb-hotplug"
chmod 0755 "$ROOT/projects/APPLaunch/dist/APPLaunch/adb/cardputer-adb-hotplug"

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
ln -f "$PACKAGE" "$OUTPUT_DIR/applaunch_arm64.deb"
(
    cd "$OUTPUT_DIR"
    sha256sum applaunch_arm64.deb > applaunch_arm64.deb.sha256
    sha256sum -c applaunch_arm64.deb.sha256
)
printf '1\n' >"$OUTPUT_DIR/applaunch_arm64.deb.update-abi"
printf '%s\n' "$PACKAGE" >"$OUTPUT_DIR/applaunch-package.path"
echo "Stable package: $OUTPUT_DIR/applaunch_arm64.deb"
echo "Checksum: $OUTPUT_DIR/applaunch_arm64.deb.sha256"
echo "Update ABI: $OUTPUT_DIR/applaunch_arm64.deb.update-abi"
echo "Result path: $OUTPUT_DIR/applaunch-package.path"
