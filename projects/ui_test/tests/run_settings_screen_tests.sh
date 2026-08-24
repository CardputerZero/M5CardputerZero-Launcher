#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${TMPDIR:-/tmp}/settings-screen-tests
binary="$build_dir/test_settings_screen_api"
mkdir -p "$build_dir"
trap 'rm -f "$binary"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/main/ui" \
    -I"$root/main/ui/model" \
    "$root/main/ui/model/setup_value_policy.cpp" \
    "$root/tests/test_settings_screen_api.cpp" \
    -o "$binary"
"$binary"

printf '%s\n' 'settings screen tests passed'
