#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
eventpp_include=${EVENTPP_INCLUDE:-"$root/../../SDK/github_source/eventpp/include"}
binary="${TMPDIR:-/tmp}/ui_test_settings_audio_api.$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    -I"$eventpp_include" \
    "$root/tests/test_settings_audio_api.cpp" \
    "$root/main/ui/settings_audio_api.cpp" \
    -o "$binary"
"$binary"
printf '%s\n' 'settings audio tests passed'

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/main/ui" \
    -I"$root/../../ext_components/cp0_lvgl/include" \
    -I"$eventpp_include" \
    "$root/tests/test_settings_sound_card.cpp" \
    -o "$binary"
"$binary"
printf '%s\n' 'settings sound card tests passed'
