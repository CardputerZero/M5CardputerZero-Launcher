#!/bin/sh
set -eu
build_dir="${TMPDIR:-/tmp}/cp0-lvgl-tests"
mkdir -p "$build_dir"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$(dirname "$0")/test_update_job.cpp" -o "$build_dir/test_update_job"
"$build_dir/test_update_job"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$(dirname "$0")/test_launcher_updater.cpp" -o "$build_dir/test_launcher_updater"
"$build_dir/test_launcher_updater"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="${TMPDIR:-/tmp}/cp0_lvgl_test_async_utils.$$"
init_plan_object="${TMPDIR:-/tmp}/cp0_lvgl_test_init_plan.$$"
esc_state_object="${TMPDIR:-/tmp}/cp0_esc_state.$$.$$.o"
gc_sections_flag="-Wl,--gc-sections"
if [ "$(uname -s)" = "Darwin" ]; then
    gc_sections_flag="-Wl,-dead_strip"
fi
trap 'rm -f "$binary" "$init_plan_object" "$esc_state_object"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$root/include" \
    -c "$root/src/cp0_esc_state.c" -o "$esc_state_object"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_c_api_boundary.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_audio_runtime_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src/cp0" \
    -I"$root/../Miniaudio/include" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0/cp0_audio_system_sound_player.cpp" \
    "$root/tests/test_audio_system_sound_player.cpp" -ldl -lm -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_async_testable_utils.cpp" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_keyboard_input_lifecycle.c" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_keyboard_thread_lifecycle.c" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sync_signal.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_audio_mixer_control.cpp" \
    "$root/tests/test_audio_mixer_control.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_integer_codec.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_bluetooth_api_contract.cpp" \
    "$root/tests/test_bluetooth_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_bluetooth_error_policy.cpp" \
    "$root/tests/test_bluetooth_error_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_pointer_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_font_cache_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src/cp0" \
    "$root/tests/test_wifi_error_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" \
    "$root/tests/test_bounded_task_registry.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/tests/test_runner_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_runner_resume_callback.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/tests/test_init_once.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/tests/test_signal_registration.cpp" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/src" -c "$root/src/cp0_init_plan.c" -o "$init_plan_object"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$init_plan_object" \
    "$root/tests/test_init_plan.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_display_screenshot_contract.cpp" \
    "$root/tests/test_display_screenshot_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_audio_api_contract.cpp" \
    "$root/tests/test_audio_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_camera_api_contract.cpp" \
    "$root/tests/test_camera_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_camera_api_contract.cpp" \
    "$root/src/cp0/cp0_camera_viewport.cpp" \
    "$root/src/sdl/sdl_lvgl_camera.cpp" \
    "$root/tests/test_sdl_camera_shutdown.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_framebuffer_codec.cpp" \
    "$root/tests/test_framebuffer_codec.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/src/cp0_alsa_parser.cpp" \
    "$root/tests/test_alsa_parser.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_alsa_parser.cpp" \
    "$root/src/cp0_soundcard_contract.cpp" \
    "$root/tests/test_soundcard_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_camera_viewport.cpp" \
    "$root/tests/test_camera_viewport.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_desktop_exec_policy.cpp" \
    "$root/tests/test_desktop_exec_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/tests/test_process_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_lora_runtime_policy.cpp" \
    "$root/tests/test_lora_runtime_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/tests/test_lora_gpio_offset_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_lora_contract.cpp" \
    "$root/tests/test_lora_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_lora_contract.cpp" \
    "$root/src/sdl/sdl_lvgl_lara.cpp" \
    "$root/tests/test_sdl_lora_registration.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_network_policy.cpp" \
    "$root/tests/test_network_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_network_api_contract.cpp" \
    "$root/tests/test_network_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -ffunction-sections -fdata-sections "$gc_sections_flag" \
    -I"$root/include" -I"$root/src" -I"$root/src/cp0" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0/cp0_process_commands.cpp" \
    "$root/tests/test_process_command_timeout.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_callback_result.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_callback_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_callback_owner_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_lvgl_callback_boundary.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sudo_timer_callback_boundary.cpp" -o "$binary"
"$binary"

if [ "$(uname -s)" = "Linux" ]; then
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
        -I"$root/src" "$root/tests/test_external_process_group.cpp" -o "$binary"
    "$binary"
fi

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_esc_exit_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$esc_state_object" \
    "$root/src/sdl/sdl_external_app_runner.cpp" \
    "$root/tests/test_sdl_external_app_runner.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_filesystem_api.cpp" \
    "$root/src/cp0_posix_filesystem.cpp" \
    "$root/tests/test_posix_filesystem.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_resource_path_policy.cpp" \
    "$root/tests/test_resource_path_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_filesystem_api.cpp" \
    "$root/src/cp0_posix_filesystem.cpp" \
    "$root/src/cp0_resource_path_policy.cpp" \
    "$root/src/sdl/sdl_lvgl_filesystem.cpp" \
    "$root/tests/test_sdl_filesystem_api.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_dispatch_testable.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_dispatch_fallback.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_battery_testable.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_battery_codec.cpp" \
    "$root/tests/test_battery_codec.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/tests/test_battery_snapshot_cache.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" \
    "$root/src/cp0_battery_lifecycle.cpp" \
    "$root/tests/test_battery_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_battery_timer_release.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_battery_runtime_shutdown.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_runner_service_shutdown.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_rpc_runtime_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/tests/test_dispatch_shutdown_gate.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/src/cp0_battery_lifecycle.cpp" \
    "$root/tests/test_battery_init_failure.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" \
    "$root/src/cp0_status_lifecycle.cpp" \
    "$root/tests/test_status_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_status_component_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_owned_component_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_status_mount_rollback.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_component_delete_callback.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_screen_component_detach.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_screen_delete_cleanup.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_battery_codec.cpp" \
    "$root/src/cp0_battery_api_contract.cpp" \
    "$root/tests/test_battery_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_battery_codec.cpp" \
    "$root/src/cp0_battery_api_contract.cpp" \
    "$root/src/cp0_battery_lifecycle.cpp" \
    "$root/src/sdl/sdl_lvgl_bq27220.cpp" \
    "$root/tests/test_sdl_bq27220_api.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_osinfo_codec.cpp" \
    "$root/tests/test_osinfo_codec.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_osinfo_codec.cpp" \
    "$root/src/cp0_osinfo_contract.cpp" \
    "$root/tests/test_osinfo_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_settings_policy.cpp" \
    "$root/tests/test_settings_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_pty_contract.cpp" \
    "$root/tests/test_pty_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_pty_contract.cpp" \
    "$root/src/sdl/sdl_lvgl_pty.cpp" \
    "$root/tests/test_pty_runtime.cpp" -lutil -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_config_service.cpp" \
    "$root/tests/test_config_service.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" "$root/src/cp0_sudo_coordinator.cpp" \
    "$root/tests/test_sudo_coordinator.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sudo_prompt_key_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_sudo_request_factory.cpp" \
    "$root/tests/test_sudo_request_factory.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" \
    "$root/tests/test_sudo_worker_registry.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sudo_command.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sudo_c_api_boundary.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" "$root/src/cp0_process_runner.cpp" \
    "$root/tests/test_process_runner.cpp" -o "$binary"
"$binary"

if [ "$(uname -s)" = "Linux" ]; then
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -I"$root/include" -I"$root/src/cp0" \
        "$root/src/cp0/cp0_keyboard_keymap.c" \
        "$root/tests/test_keyboard_keymap.c" -lxkbcommon -o "$binary"
    "$binary"
fi

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_keyboard_text.c" \
    "$root/src/cp0_esc_state.c" \
    "$root/src/cp0_keyboard_queue.c" \
    "$root/tests/test_keyboard_contract.c" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=thread -g \
    -I"$root/include" "$root/src/cp0_esc_state.c" \
    "$root/tests/test_esc_state_tsan.c" -o "$binary"
TSAN_OPTIONS="halt_on_error=1" "$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_keyboard_input_context.cpp" -o "$binary"
"$binary"
