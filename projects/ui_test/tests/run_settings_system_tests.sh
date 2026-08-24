#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${TMPDIR:-/tmp}/settings-system-tests
mkdir -p "$build_dir"
eventpp_include=${EVENTPP_INCLUDE:-"$root/../../SDK/github_source/eventpp/include"}

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    -I"$root/../../ext_components/cp0_lvgl/src" \
    -I"$eventpp_include" \
    "$root/tests/test_settings_system_model.cpp" \
    "$root/main/ui/settings_system_model.cpp" \
    -o "$build_dir/test_settings_system_model"

"$build_dir/test_settings_system_model"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    -I"$root/../../ext_components/cp0_lvgl/src" \
    -I"$eventpp_include" \
    "$root/main/ui/settings_system_api.cpp" \
    "$root/main/ui/settings_system_model.cpp" \
    "$root/tests/test_settings_system_api.cpp" \
    -o "$build_dir/test_settings_system_api"

"$build_dir/test_settings_system_api"
printf '%s\n' 'settings system tests passed'
