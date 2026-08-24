#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${TMPDIR:-/tmp}/settings-adb-tests
mkdir -p "$BUILD_DIR"

CXX=${CXX:-g++}
COMMON_FLAGS="-std=c++17 -Wall -Wextra -Werror -pthread -I$ROOT/main/ui"

$CXX $COMMON_FLAGS \
    "$ROOT/tests/test_settings_adb_state.cpp" \
    "$ROOT/main/ui/settings_adb_state.cpp" \
    -o "$BUILD_DIR/test_settings_adb_state"
"$BUILD_DIR/test_settings_adb_state"

$CXX $COMMON_FLAGS -DSETTINGS_ADB_API_NO_CP0 \
    "$ROOT/tests/test_settings_adb_api.cpp" \
    "$ROOT/main/ui/settings_adb_api.cpp" \
    "$ROOT/main/ui/settings_adb_state.cpp" \
    -o "$BUILD_DIR/test_settings_adb_api"
"$BUILD_DIR/test_settings_adb_api"

printf '%s\n' 'settings ADB tests passed'
