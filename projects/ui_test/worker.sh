#!/usr/bin/env bash
set -euo pipefail

PROJECT_PATH=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '8')}
WORKSPACE_ROOT=${CARDPUTERZERO_ROOT:-$(CDPATH= cd -- "$PROJECT_PATH/../.." && pwd -P)}

usage() {
    cat >&2 <<'EOF'
usage:
  worker.sh validate [integration-settings-link]
  worker.sh info
  worker.sh sdl [scons arguments]
  worker.sh cp0 [scons arguments]
  worker.sh distclean
EOF
}

require_scons() {
    command -v scons >/dev/null 2>&1 || {
        printf 'scons is required; install it in the active Python environment\n' >&2
        exit 1
    }
}

case "${1:-}" in
    validate)
        shift
        exec "$PROJECT_PATH/validate_worker.sh" "$PROJECT_PATH" "$@"
        ;;
    info)
        printf 'worker: %s\n' "$PROJECT_PATH"
        printf 'python: '
        python3 --version
        printf 'scons: '
        scons --version | sed -n '2p'
        printf 'host compiler: '
        gcc --version | sed -n '1p'
        if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
            printf 'cross compiler: '
            aarch64-linux-gnu-gcc --version | sed -n '1p'
        else
            printf 'cross compiler: not found\n'
        fi
        if [ -f "$WORKSPACE_ROOT/ext_components/cp0_lvgl/sdk_version.txt" ]; then
            printf 'cp0 SDK version: '
            tr -d '\n' < "$WORKSPACE_ROOT/ext_components/cp0_lvgl/sdk_version.txt"
            printf '\n'
        fi
        ;;
    sdl|cp0)
        mode=$1
        shift
        require_scons
        cd "$PROJECT_PATH"
        if [ "$mode" = sdl ]; then
            export CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk
        else
            export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
        fi
        exec scons -j"$JOBS" "$@"
        ;;
    distclean)
        shift
        require_scons
        cd "$PROJECT_PATH"
        exec scons distclean "$@"
        ;;
    *)
        usage
        exit 2
        ;;
esac
