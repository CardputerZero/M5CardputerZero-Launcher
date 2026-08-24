#!/usr/bin/env bash
set -euo pipefail

TEMPLATE=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
TEMP_ROOT=$(mktemp -d /tmp/settings-ui-isolation-test.XXXXXX)
trap 'rm -rf "$TEMP_ROOT"' EXIT

mkdir -p "$TEMP_ROOT/projects" "$TEMP_ROOT/SDK/tools/scons" \
    "$TEMP_ROOT/SDK/github_source/static_lib_v0.0.6" \
    "$TEMP_ROOT/ext_components/cp0_lvgl"
printf 'v0.0.6\n' > "$TEMP_ROOT/ext_components/cp0_lvgl/sdk_version.txt"
printf 'v0.0.6\n' > "$TEMP_ROOT/SDK/github_source/static_lib_v0.0.6/version"
cat > "$TEMP_ROOT/SDK/tools/scons/project.py" <<'EOF'
env = {'PROJECT_TOOL_S': 'fake_tool.py'}
Return('env')
EOF
printf '# fake\n' > "$TEMP_ROOT/SDK/tools/scons/fake_tool.py"

prepare_worker() {
    local name=$1
    cp -a -- "$TEMPLATE" "$TEMP_ROOT/projects/$name"
    rm -rf -- "$TEMP_ROOT/projects/$name/build" \
        "$TEMP_ROOT/projects/$name/dist" \
        "$TEMP_ROOT/projects/$name/.cache" \
        "$TEMP_ROOT/projects/$name/SDK" \
        "$TEMP_ROOT/projects/$name/compile_commands.json"
}

run_config() {
    local worker=$1
    local config=$2
    CARDPUTERZERO_ROOT="$TEMP_ROOT" CONFIG_DEFAULT_FILE="$config" \
        scons -Q -n -f "$TEMP_ROOT/projects/$worker/SConstruct" >/dev/null
}

prepare_worker ui_test2
"$TEMP_ROOT/projects/ui_test2/validate_worker.sh" \
    "$TEMP_ROOT/projects/ui_test2"
run_config ui_test2 linux_x86_sdl2_config_defaults.mk

printf 'stale object\n' > "$TEMP_ROOT/projects/ui_test2/build/stale.o"
mkdir -p "$TEMP_ROOT/projects/ui_test2/dist"
printf 'stale artifact\n' > "$TEMP_ROOT/projects/ui_test2/dist/stale"
printf 'stale config\n' > "$TEMP_ROOT/projects/ui_test2/build/config/global_config.mk"
run_config ui_test2 linux_x86_cross_cp0_config_defaults.mk

[ ! -e "$TEMP_ROOT/projects/ui_test2/build/stale.o" ]
[ ! -e "$TEMP_ROOT/projects/ui_test2/dist/stale" ]
[ ! -e "$TEMP_ROOT/projects/ui_test2/build/config/global_config.mk" ]
[ -f "$TEMP_ROOT/projects/ui_test2/build/config/config_tmp.mk" ]
grep -F 'static-lib-version=v0.0.6' \
    "$TEMP_ROOT/projects/ui_test2/build/config/selected-defaults.txt" >/dev/null

run_config ui_test2 linux_x86_sdl2_config_defaults.mk
[ ! -e "$TEMP_ROOT/projects/ui_test2/build/config/config_tmp.mk" ]

prepare_worker ui_test3
(
    run_config ui_test2 linux_x86_sdl2_config_defaults.mk
) & first_pid=$!
(
    run_config ui_test3 linux_x86_sdl2_config_defaults.mk
) & second_pid=$!
wait "$first_pid"
wait "$second_pid"

grep -F "$TEMP_ROOT/projects/ui_test2/linux_x86_sdl2_config_defaults.mk" \
    "$TEMP_ROOT/projects/ui_test2/build/config/selected-defaults.txt" >/dev/null
grep -F "$TEMP_ROOT/projects/ui_test3/linux_x86_sdl2_config_defaults.mk" \
    "$TEMP_ROOT/projects/ui_test3/build/config/selected-defaults.txt" >/dev/null

printf 'T02 build isolation checks passed\n'
