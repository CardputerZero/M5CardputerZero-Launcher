#!/bin/sh
set -eu
python3 "$(dirname "$0")/test_store_cache_sync.py"
python3 "$(dirname "$0")/test_wifi_view_lifecycle_contract.py"
python3 "$(dirname "$0")/test_carousel_border_contract.py"
python3 "$(dirname "$0")/test_bluetooth_power_contract.py"
python3 "$(dirname "$0")/test_external_framebuffer_ownership.py"
PYTHONPATH="$(dirname "$0")/..${PYTHONPATH:+:$PYTHONPATH}" \
    python3 "$(dirname "$0")/test_config_default_file.py"
build_dir="${TMPDIR:-/tmp}/applaunch-tests"
mkdir -p "$build_dir"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_low_battery_flow.cpp" \
    -o "$build_dir/test_low_battery_flow"
"$build_dir/test_low_battery_flow"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_rtc_state_model.cpp" \
    "$(dirname "$0")/../main/ui/model/rtc_state_model.cpp" \
    -o "$build_dir/test_rtc_state_model"
"$build_dir/test_rtc_state_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$(dirname "$0")/test_async_operation_lifecycle.cpp" \
    "$(dirname "$0")/../main/ui/model/async_operation_lifecycle.cpp" \
    -o "$build_dir/test_async_operation_lifecycle"
"$build_dir/test_async_operation_lifecycle"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$(dirname "$0")/test_page_timer_lifecycle.cpp" \
    -o "$build_dir/test_page_timer_lifecycle"
"$build_dir/test_page_timer_lifecycle"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_setup_info_model.cpp" \
    "$(dirname "$0")/../main/ui/model/setup_info_model.cpp" \
    -o "$build_dir/test_setup_info_model"
"$build_dir/test_setup_info_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_setup_wifi_model.cpp" \
    "$(dirname "$0")/../main/ui/model/setup_wifi_model.cpp" \
    -o "$build_dir/test_setup_wifi_model"
"$build_dir/test_setup_wifi_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_adb_state.cpp" \
    "$(dirname "$0")/../main/ui/model/adb_state.cpp" \
    -o "$build_dir/test_adb_state"
"$build_dir/test_adb_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_developer_page_model.cpp" \
    "$(dirname "$0")/../main/ui/model/developer_page_model.cpp" \
    -o "$build_dir/test_developer_page_model"
"$build_dir/test_developer_page_model"
"$(dirname "$0")/test_cardputer_adb.sh"
python3 "$(dirname "$0")/test_adb_packaging.py"
python3 "$(dirname "$0")/test_updater_packaging.py"
python3 "$(dirname "$0")/test_appstore_packaging.py"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/include" \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    -I"$(dirname "$0")/../../../SDK/github_source/eventpp/include" \
    "$(dirname "$0")/test_launcher_platform.cpp" \
    -o "$build_dir/test_launcher_platform"
"$build_dir/test_launcher_platform"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_desktop_entry.cpp" \
    "$(dirname "$0")/../main/ui/desktop_entry.cpp" \
    -o "$build_dir/test_desktop_entry"
"$build_dir/test_desktop_entry"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_snake_game_model.cpp" \
    "$(dirname "$0")/../main/ui/model/snake_game_model.cpp" \
    -o "$build_dir/test_snake_game_model"
"$build_dir/test_snake_game_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_game_input_action.cpp" \
    -o "$build_dir/test_game_input_action"
"$build_dir/test_game_input_action"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_snake_view_contract.cpp" \
    -o "$build_dir/test_snake_view_contract"
"$build_dir/test_snake_view_contract"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_sound_card_model.cpp" \
    "$(dirname "$0")/../main/ui/model/sound_card_model.cpp" \
    -o "$build_dir/test_sound_card_model"
"$build_dir/test_sound_card_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_ip_panel_model.cpp" \
    "$(dirname "$0")/../main/ui/model/ip_panel_model.cpp" \
    -o "$build_dir/test_ip_panel_model"
"$build_dir/test_ip_panel_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_ssh_connection_model.cpp" \
    "$(dirname "$0")/../main/ui/model/ssh_connection_model.cpp" \
    "$(dirname "$0")/../main/ui/keyboard_text_input.cpp" \
    -o "$build_dir/test_ssh_connection_model"
"$build_dir/test_ssh_connection_model"

${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_ssh_view_build_contract.cpp" \
    -o "$build_dir/test_ssh_view_build_contract"
"$build_dir/test_ssh_view_build_contract"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_keyboard_text_input.cpp" \
    "$(dirname "$0")/../main/ui/keyboard_text_input.cpp" \
    -o "$build_dir/test_keyboard_text_input"
"$build_dir/test_keyboard_text_input"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_mesh_page_model.cpp" \
    "$(dirname "$0")/../main/ui/model/mesh_page_model.cpp" \
    -o "$build_dir/test_mesh_page_model"
"$build_dir/test_mesh_page_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_lora_page_model.cpp" \
    "$(dirname "$0")/../main/ui/model/lora_page_model.cpp" \
    -o "$build_dir/test_lora_page_model"
"$build_dir/test_lora_page_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_tank_battle_model.cpp" \
    "$(dirname "$0")/../main/ui/model/tank_battle_model.cpp" \
    -o "$build_dir/test_tank_battle_model"
"$build_dir/test_tank_battle_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_setup_page_model.cpp" \
    "$(dirname "$0")/../main/ui/model/setup_page_model.cpp" \
    -o "$build_dir/test_setup_page_model"
"$build_dir/test_setup_page_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_setup_view_build_contract.cpp" \
    -o "$build_dir/test_setup_view_build_contract"
"$build_dir/test_setup_view_build_contract"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_setup_page_access_policy.cpp" \
    "$(dirname "$0")/../main/ui/page_app/setting/setup_page_access_policy.cpp" \
    -o "$build_dir/test_setup_page_access_policy"
"$build_dir/test_setup_page_access_policy"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_setup_value_policy.cpp" \
    "$(dirname "$0")/../main/ui/model/setup_value_policy.cpp" \
    -o "$build_dir/test_setup_value_policy"
"$build_dir/test_setup_value_policy"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_boot_action_policy.cpp" \
    "$(dirname "$0")/../main/ui/model/boot_action_policy.cpp" \
    -o "$build_dir/test_boot_action_policy"
"$build_dir/test_boot_action_policy"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_system_page_model.cpp" \
    "$(dirname "$0")/../main/ui/model/system_page_model.cpp" \
    -o "$build_dir/test_system_page_model"
"$build_dir/test_system_page_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_screensaver_model.cpp" \
    "$(dirname "$0")/../main/ui/model/screensaver_model.cpp" \
    -o "$build_dir/test_screensaver_model"
"$build_dir/test_screensaver_model"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../../../ext_components/cp0_lvgl/include" \
    "$(dirname "$0")/test_esc_hold_lifecycle_model.cpp" \
    "$(dirname "$0")/../main/ui/model/esc_hold_lifecycle_model.cpp" \
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
    "$(dirname "$0")/test_bluetooth_page_model.cpp" \
    "$(dirname "$0")/../main/ui/model/bluetooth_page_model.cpp" \
    -o "$build_dir/test_bluetooth_page_model"
"$build_dir/test_bluetooth_page_model"
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
