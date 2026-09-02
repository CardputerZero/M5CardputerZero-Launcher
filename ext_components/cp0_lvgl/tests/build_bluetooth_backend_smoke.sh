#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-/tmp/cp0_bluetooth_backend_smoke}
pkg_flags=$(pkg-config --cflags --libs gio-2.0)

cc=${CXX:-c++}
$cc -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
    -I"$root/src" -I"$root/include" \
    "$root/tests/bluetooth_backend_smoke.cpp" \
    "$root/src/cp0/cp0_bluez_dbus_client.cpp" \
    "$root/src/cp0/cp0_bluetooth_error_policy.cpp" \
    "$root/src/cp0/cp0_lvgl_log.cpp" \
    $pkg_flags -o "$out"
printf '%s\n' "$out"
