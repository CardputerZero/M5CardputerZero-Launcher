#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
TEMPLATE="$ROOT/projects/ui_test"

if [ "$#" -ne 1 ]; then
    printf 'usage: %s /path/to/ui_testN\n' "$0" >&2
    exit 2
fi

DEST=$1
case "$DEST" in
    /*) ;;
    *) DEST="$PWD/$DEST" ;;
esac
DEST=$(dirname -- "$DEST")/$(basename -- "$DEST")

case "$DEST/" in
    "$TEMPLATE/"*)
        printf 'destination must not be inside the template: %s\n' "$DEST" >&2
        exit 2
        ;;
esac

[ -d "$TEMPLATE" ] || {
    printf 'worker template not found: %s\n' "$TEMPLATE" >&2
    exit 1
}
[ ! -e "$DEST" ] || {
    printf 'destination already exists: %s\n' "$DEST" >&2
    exit 1
}

mkdir -p -- "$(dirname -- "$DEST")"
cp -a -- "$TEMPLATE" "$DEST"
rm -rf -- \
    "$DEST/build" \
    "$DEST/dist" \
    "$DEST/.cache" \
    "$DEST/SDK" \
    "$DEST/compile_commands.json" \
    "$DEST/.sconsign.dblite" \
    "$DEST/.config" \
    "$DEST/.config.old" \
    "$DEST/.config.mk"

"$DEST/validate_worker.sh" "$DEST"
printf 'created worker: %s\n' "$DEST"
printf 'build with: %s/worker.sh sdl\n' "$DEST"
