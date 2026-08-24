#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${TMPDIR:-/tmp}/settings-battery-tests
mkdir -p "$build_dir"

binary="$build_dir/test_settings_battery_info"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    -I"$root/../../ext_components/cp0_lvgl/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/main/ui/settings_page_battery_info_model.cpp" \
    "$root/main/ui/settings_page_battery_api.cpp" \
    "$root/tests/test_settings_battery_info.cpp" \
    -o "$binary"

"$binary"
printf '%s\n' 'settings battery tests passed'
