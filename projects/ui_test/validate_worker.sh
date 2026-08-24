#!/usr/bin/env bash
set -euo pipefail

WORKER_PATH=${1:-$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)}
WORKER_PATH=$(CDPATH= cd -- "$WORKER_PATH" && pwd -P)

fail() {
    printf 'worker validation failed: %s\n' "$1" >&2
    exit 1
}

for required_path in \
    "$WORKER_PATH/SConstruct" \
    "$WORKER_PATH/main/SConstruct" \
    "$WORKER_PATH/main/ui" \
    "$WORKER_PATH/APPLaunch" \
    "$WORKER_PATH/APPLaunch/applications/ui_test.desktop"; do
    [ -e "$required_path" ] || fail "missing $required_path"
done

grep -Fx 'Exec=/usr/share/APPLaunch/bin/ui_test' \
    "$WORKER_PATH/APPLaunch/applications/ui_test.desktop" >/dev/null ||
    fail "desktop entry does not target the worker executable ui_test"

resolved_ui=$(readlink -f -- "$WORKER_PATH/main/ui") || fail "cannot resolve main/ui"
case "$resolved_ui" in
    "$WORKER_PATH/main/ui"|"$WORKER_PATH/main/ui"/*) ;;
    *) fail "main/ui resolves outside this worker: $resolved_ui" ;;
esac

resolved_static=$(readlink -f -- "$WORKER_PATH/APPLaunch") || fail "cannot resolve APPLaunch"
case "$resolved_static" in
    "$WORKER_PATH/APPLaunch"|"$WORKER_PATH/APPLaunch"/*) ;;
    *) fail "APPLaunch resources resolve outside this worker: $resolved_static" ;;
esac

for source_root in "$WORKER_PATH/main" "$WORKER_PATH/APPLaunch"; do
    while IFS= read -r -d '' link_path; do
        resolved_target=$(readlink -f -- "$link_path") || fail "dangling symlink: $link_path"
        case "$resolved_target" in
            "$WORKER_PATH"|"$WORKER_PATH"/*) ;;
            *) fail "source/resource symlink escapes this worker: $link_path -> $resolved_target" ;;
        esac
    done < <(find "$source_root" -type l -print0)
done

settings_link=${2:-}
if [ -n "$settings_link" ]; then
    settings_link=$(CDPATH= cd -- "$(dirname -- "$settings_link")" && pwd -P)/$(basename -- "$settings_link")
    [ -L "$settings_link" ] || fail "integration settings path is not a symlink: $settings_link"
    resolved_settings=$(readlink -f -- "$settings_link") || fail "cannot resolve $settings_link"
    [ "$resolved_settings" = "$resolved_ui" ] ||
        fail "integration settings link points to another worker: $settings_link -> $resolved_settings"
fi

printf 'worker: %s\n' "$WORKER_PATH"
printf 'ui source: %s\n' "$resolved_ui"
printf 'static resources: %s\n' "$resolved_static"
printf 'build output: %s\n' "$WORKER_PATH/build"
printf 'config output: %s\n' "$WORKER_PATH/build/config"
printf 'cache output: %s\n' "$WORKER_PATH/.cache"
