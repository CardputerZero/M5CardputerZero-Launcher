#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
settings_ui_root=$(CDPATH= cd -- "$test_root/../main/ui/settings" && pwd)
python3 "$test_root/test_store_cache_sync.py"
python3 "$test_root/test_carousel_border_contract.py"
python3 "$test_root/test_external_framebuffer_ownership.py"
python3 "$test_root/test_settings_input_font_contract.py"
PYTHONPATH="$test_root/..${PYTHONPATH:+:$PYTHONPATH}" \
    python3 "$test_root/test_config_default_file.py"
build_dir="${TMPDIR:-/tmp}/applaunch-tests"
mkdir -p "$build_dir"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_low_battery_flow.cpp" \
    -o "$build_dir/test_low_battery_flow"
"$build_dir/test_low_battery_flow"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$test_root/test_async_operation_lifecycle.cpp" \
    "$test_root/../main/ui/model/async_operation_lifecycle.cpp" \
    -o "$build_dir/test_async_operation_lifecycle"
"$build_dir/test_async_operation_lifecycle"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$test_root/test_page_timer_lifecycle.cpp" \
    -o "$build_dir/test_page_timer_lifecycle"
"$build_dir/test_page_timer_lifecycle"
python3 "$test_root/test_updater_packaging.py"
python3 "$test_root/test_appstore_packaging.py"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$test_root/../main/include" \
    -I"$test_root/../../../ext_components/cp0_lvgl/include" \
    -I"$test_root/../../../SDK/github_source/eventpp/include" \
    "$test_root/test_launcher_platform.cpp" \
    -o "$build_dir/test_launcher_platform"
"$build_dir/test_launcher_platform"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$test_root/../main/ui" \
    "$test_root/test_desktop_entry.cpp" \
    "$test_root/../main/ui/desktop_entry.cpp" \
    -o "$build_dir/test_desktop_entry"
"$build_dir/test_desktop_entry"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_snake_game_model.cpp" \
    "$test_root/../main/ui/model/snake_game_model.cpp" \
    -o "$build_dir/test_snake_game_model"
"$build_dir/test_snake_game_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$test_root/../../../ext_components/cp0_lvgl/include" \
    "$test_root/test_game_input_action.cpp" \
    -o "$build_dir/test_game_input_action"
"$build_dir/test_game_input_action"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_snake_view_contract.cpp" \
    -o "$build_dir/test_snake_view_contract"
"$build_dir/test_snake_view_contract"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_ip_panel_model.cpp" \
    "$test_root/../main/ui/model/ip_panel_model.cpp" \
    -o "$build_dir/test_ip_panel_model"
"$build_dir/test_ip_panel_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$test_root/../../../ext_components/cp0_lvgl/include" \
    "$test_root/test_ssh_connection_model.cpp" \
    "$test_root/../main/ui/model/ssh_connection_model.cpp" \
    "$test_root/../main/ui/keyboard_text_input.cpp" \
    -o "$build_dir/test_ssh_connection_model"
"$build_dir/test_ssh_connection_model"

${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_ssh_view_build_contract.cpp" \
    -o "$build_dir/test_ssh_view_build_contract"
"$build_dir/test_ssh_view_build_contract"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$test_root/../../../ext_components/cp0_lvgl/include" \
    "$test_root/test_keyboard_text_input.cpp" \
    "$test_root/../main/ui/keyboard_text_input.cpp" \
    -o "$build_dir/test_keyboard_text_input"
"$build_dir/test_keyboard_text_input"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_mesh_page_model.cpp" \
    "$test_root/../main/ui/model/mesh_page_model.cpp" \
    -o "$build_dir/test_mesh_page_model"
"$build_dir/test_mesh_page_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_lora_page_model.cpp" \
    "$test_root/../main/ui/model/lora_page_model.cpp" \
    -o "$build_dir/test_lora_page_model"
"$build_dir/test_lora_page_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_tank_battle_model.cpp" \
    "$test_root/../main/ui/model/tank_battle_model.cpp" \
    -o "$build_dir/test_tank_battle_model"
"$build_dir/test_tank_battle_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_setup_value_policy.cpp" \
    "$test_root/../main/ui/model/setup_value_policy.cpp" \
    -o "$build_dir/test_setup_value_policy"
"$build_dir/test_setup_value_policy"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$settings_ui_root" \
    "$test_root/test_settings_t12b_models.cpp" \
    "$settings_ui_root/settings_t12b_about_help_model.cpp" \
    "$settings_ui_root/settings_t12b_boot_action_policy.cpp" \
    "$settings_ui_root/settings_t12b_extport_model.cpp" \
    "$settings_ui_root/settings_t12b_launcher_model.cpp" \
    -o "$build_dir/test_settings_t12b_models"
"$build_dir/test_settings_t12b_models"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$settings_ui_root" \
    "$test_root/test_settings_battery_info_model.cpp" \
    "$settings_ui_root/settings_page_battery_info_model.cpp" \
    -o "$build_dir/test_settings_battery_info_model"
"$build_dir/test_settings_battery_info_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$settings_ui_root" \
    "$test_root/test_settings_about_info_model.cpp" \
    "$settings_ui_root/settings_about_info_model.cpp" \
    -o "$build_dir/test_settings_about_info_model"
"$build_dir/test_settings_about_info_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$settings_ui_root" \
    "$test_root/test_settings_system_update_model.cpp" \
    "$settings_ui_root/settings_system_model.cpp" \
    -o "$build_dir/test_settings_system_update_model"
"$build_dir/test_settings_system_update_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$test_root/../../../ext_components/cp0_lvgl/tests/test_update_job.cpp" \
    -o "$build_dir/test_update_job"
"$build_dir/test_update_job"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$settings_ui_root" \
    -I"$test_root/../../../ext_components/cp0_lvgl/include" \
    -I"$test_root/../../../SDK/github_source/eventpp/include" \
    "$test_root/test_settings_t12b_adapter.cpp" \
    "$settings_ui_root/settings_t12b_adapter.cpp" \
    "$settings_ui_root/settings_t12b_boot_action_policy.cpp" \
    "$settings_ui_root/settings_t12b_extport_model.cpp" \
    "$settings_ui_root/settings_t12b_launcher_model.cpp" \
    -o "$build_dir/test_settings_t12b_adapter"
"$build_dir/test_settings_t12b_adapter"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$test_root/test_screensaver_model.cpp" \
    "$test_root/../main/ui/model/screensaver_model.cpp" \
    -o "$build_dir/test_screensaver_model"
"$build_dir/test_screensaver_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$test_root/../../../ext_components/cp0_lvgl/include" \
    "$test_root/test_esc_hold_lifecycle_model.cpp" \
    "$test_root/../main/ui/model/esc_hold_lifecycle_model.cpp" \
    -o "$build_dir/test_esc_hold_lifecycle_model"
"$build_dir/test_esc_hold_lifecycle_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_esc_ui_watchdog.cpp" \
    -o "$build_dir/test_esc_ui_watchdog"
"$build_dir/test_esc_ui_watchdog"
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    -c "$(dirname "$0")/../../../ext_components/cp0_lvgl/src/cp0_esc_state.c" \
    -o "$build_dir/cp0_esc_state.o"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_esc_ui_watchdog_recovery.cpp" \
    "$build_dir/cp0_esc_state.o" \
    "$(dirname "$0")/../main/ui/esc_ui_watchdog.cpp" \
    -o "$build_dir/test_esc_ui_watchdog_recovery"
"$build_dir/test_esc_ui_watchdog_recovery"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_launcher_navigation_model.cpp" \
    "$(dirname "$0")/../main/ui/model/launcher_navigation_model.cpp" \
    -o "$build_dir/test_launcher_navigation_model"
"$build_dir/test_launcher_navigation_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$(dirname "$0")/test_app_registry_callback.cpp" \
    -o "$build_dir/test_app_registry_callback"
"$build_dir/test_app_registry_callback"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_launcher_media_model.cpp" \
    "$(dirname "$0")/../main/ui/model/launcher_media_model.cpp" \
    -o "$build_dir/test_launcher_media_model"
"$build_dir/test_launcher_media_model"

${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    -I"$(dirname "$0")/../../../SDK/github_source/eventpp/include" \
    "$(dirname "$0")/test_launcher_media_controls.cpp" \
    "$(dirname "$0")/../main/ui/launcher_media_controls.cpp" \
    "$(dirname "$0")/../main/ui/model/launcher_media_model.cpp" \
    "$(dirname "$0")/../main/ui/model/setup_value_policy.cpp" \
    -o "$build_dir/test_launcher_media_controls"
"$build_dir/test_launcher_media_controls"

${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_global_hint_policy.cpp" \
    "$(dirname "$0")/../main/ui/model/global_hint_policy.cpp" \
    -o "$build_dir/test_global_hint_policy"
"$build_dir/test_global_hint_policy"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_global_overlay_model.cpp" \
    -o "$build_dir/test_global_overlay_model"
"$build_dir/test_global_overlay_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_st_key_encoder.cpp" \
    "$(dirname "$0")/../main/ui/model/st_key_encoder.cpp" \
    -o "$build_dir/test_st_key_encoder"
"$build_dir/test_st_key_encoder"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_terminal_unicode.cpp" \
    "$(dirname "$0")/../main/ui/model/terminal_unicode.cpp" \
    -o "$build_dir/test_terminal_unicode"
"$build_dir/test_terminal_unicode"
