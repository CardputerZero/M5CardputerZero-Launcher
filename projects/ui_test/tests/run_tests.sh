#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="${TMPDIR:-/tmp}/ui_test_bluetooth_api.$$"
async_binary="${TMPDIR:-/tmp}/ui_test_async_dispatch.$$"
state_binary="${TMPDIR:-/tmp}/ui_test_request_state.$$"
wifi_binary="${TMPDIR:-/tmp}/ui_test_wifi_api.$$"
trap 'rm -f "$binary" "$async_binary" "$state_binary" "$wifi_binary"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    "$root/tests/test_settings_wifi_api.cpp" \
    "$root/main/ui/settings_wifi_api.cpp" \
    -o "$wifi_binary"
"$wifi_binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    "$root/tests/test_settings_bluetooth_api.cpp" \
    -o "$binary"
"$binary"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/main/ui" \
    "$root/tests/test_settings_async_dispatch.cpp" \
    -o "$async_binary"
"$async_binary"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    -I"${EVENTPP_INCLUDE:-$root/../../SDK/github_source/eventpp/include}" \
    "$root/tests/test_settings_request_state.cpp" \
    -o "$state_binary"
"$state_binary"
"$root/tests/run_settings_audio_tests.sh"
"$root/tests/run_settings_screen_tests.sh"
