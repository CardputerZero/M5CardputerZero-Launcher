#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
eventpp_include=${EVENTPP_INCLUDE:-"$root/../../SDK/github_source/eventpp/include"}
build_dir=${TMPDIR:-/tmp}/settings-rtc-tests
mkdir -p "$build_dir"
trap 'rm -f "$build_dir/test_settings_rtc_model" "$build_dir/test_settings_rtc_api"' EXIT HUP INT TERM

common_flags="-std=c++17 -Wall -Wextra -Werror -pthread"

${CXX:-c++} $common_flags \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    -I"$root/../../ext_components/cp0_lvgl/src" \
    -I"$eventpp_include" \
    "$root/main/ui/settings_rtc_api.cpp" \
    "$root/tests/test_settings_rtc_model.cpp" \
    -o "$build_dir/test_settings_rtc_model"
"$build_dir/test_settings_rtc_model"

${CXX:-c++} $common_flags \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    -I"$root/../../ext_components/cp0_lvgl/src" \
    -I"$eventpp_include" \
    "$root/main/ui/settings_rtc_api.cpp" \
    "$root/tests/test_settings_rtc_api.cpp" \
    -o "$build_dir/test_settings_rtc_api"
"$build_dir/test_settings_rtc_api"

printf '%s\n' 'settings RTC tests passed'
