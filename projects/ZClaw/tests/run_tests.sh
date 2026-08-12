#!/bin/sh
set -eu
set -f

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ui_dir="$test_dir/../main/ui"
build_dir="${TMPDIR:-/tmp}/zclaw-tests"
cxx="${CXX:-g++}"
test_cxxflags="${ZCLAW_TEST_CXXFLAGS:-}"
test_ldflags="${ZCLAW_TEST_LDFLAGS:-}"
test_filter="${ZCLAW_TEST_FILTER:-}"
tests_run=0
mkdir -p "$build_dir"

"$cxx" -std=c++17 -Wall -Wextra -Werror -I"$ui_dir" \
    "$test_dir/zclaw_secret_input_model_test.cpp" \
    -o "$build_dir/zclaw_secret_input_model_test"
"$build_dir/zclaw_secret_input_model_test"

build_and_run()
{
    name=$1
    shift
    if [ -n "$test_filter" ]; then
        case " $test_filter " in
            *" $name "*) ;;
            *) return ;;
        esac
    fi
    tests_run=$((tests_run + 1))
    # Intentional field splitting: flags are whitespace-delimited, while glob
    # expansion is disabled above so flag contents cannot expand to filenames.
    "$cxx" -std=c++17 -Wall -Wextra -Werror -pthread \
        $test_cxxflags \
        -I"$ui_dir" \
        -I"$test_dir/../../../SDK/components/utilities/include" \
        -isystem "$test_dir/../../../SDK/github_source/libhv/include" \
        -I"$test_dir/../../../ext_components/cp0_lvgl/include" \
        "$@" "$test_dir/../build/hv/libhv.a" -lssl -lcrypto \
        $test_ldflags -o "$build_dir/$name"
    "$build_dir/$name"
}

build_and_run zclaw_core_test \
    "$test_dir/zclaw_core_test.cpp" \
    "$ui_dir/zclaw_approval_controller.cpp" \
    "$ui_dir/zclaw_chat_event.cpp" \
    "$ui_dir/zclaw_chat_stream_model.cpp" \
    "$ui_dir/zclaw_chat_transport.cpp" \
    "$ui_dir/zclaw_webhook_transport.cpp" \
    "$ui_dir/zclaw_lhv_http_client.cpp" \
    "$ui_dir/zclaw_websocket_transport.cpp" \
    "$ui_dir/zclaw_request_id.cpp" \
    "$ui_dir/zclaw_cli_config_model.cpp" \
    "$ui_dir/zclaw_cli_output_model.cpp" \
    "$ui_dir/zclaw_cli_service.cpp" \
    "$ui_dir/zclaw_atomic_file.cpp" \
    "$ui_dir/zclaw_directory_sync.cpp" \
    "$ui_dir/zclaw_config_codec.cpp" \
    "$ui_dir/zclaw_config_document_model.cpp" \
    "$ui_dir/zclaw_gateway_response_model.cpp" \
    "$ui_dir/zclaw_http_cancellation.cpp" \
    "$ui_dir/zclaw_http_client_policy.cpp" \
    "$ui_dir/zclaw_provider_store.cpp" \
    "$ui_dir/zclaw_provider_catalog.cpp" \
    "$ui_dir/zclaw_provider_collection_model.cpp" \
    "$ui_dir/zclaw_provider_manager.cpp" \
    "$ui_dir/zclaw_provider_form_model.cpp" \
    "$ui_dir/zclaw_protocol.cpp" \
    "$ui_dir/zclaw_path_model.cpp" \
    "$ui_dir/zclaw_paths.cpp" \
    "$ui_dir/zclaw_pairing_service.cpp" \
    "$ui_dir/zclaw_process_executor.cpp" \
    "$ui_dir/zclaw_process_runner.cpp" \
    "$ui_dir/zclaw_quickstart_model.cpp" \
    "$ui_dir/zclaw_runtime_model.cpp" \
    "$ui_dir/zclaw_runtime_state.cpp" \
    "$ui_dir/zclaw_risk_profile_model.cpp" \
    "$ui_dir/zclaw_risk_profile_store.cpp" \
    "$ui_dir/zclaw_selection_model.cpp" \
    "$ui_dir/zclaw_settings_controller.cpp" \
    "$ui_dir/zclaw_settings_navigation_model.cpp" \
    "$ui_dir/zclaw_settings_presentation.cpp" \
    "$ui_dir/zclaw_storage.cpp" \
    "$ui_dir/zclaw_task_inbox.cpp" \
    "$ui_dir/zclaw_text.cpp" \
    "$ui_dir/zclaw_text_file.cpp" \
    "$ui_dir/zclaw_ui_config_manager.cpp" \
    "$ui_dir/zclaw_ui_config_store.cpp"

build_and_run zclaw_protocol_model_test \
    "$test_dir/zclaw_protocol_model_test.cpp" \
    "$ui_dir/zclaw_chat_event.cpp" \
    "$ui_dir/zclaw_chat_layout.cpp" \
    "$ui_dir/zclaw_chat_stream_model.cpp" \
    "$ui_dir/zclaw_file_downloader.cpp" \
    "$ui_dir/zclaw_lhv_http_client.cpp" \
    "$ui_dir/zclaw_gateway_response_model.cpp" \
    "$ui_dir/zclaw_http_cancellation.cpp" \
    "$ui_dir/zclaw_http_client_policy.cpp" \
    "$ui_dir/zclaw_protocol.cpp" \
    "$ui_dir/zclaw_quickstart_model.cpp" \
    "$ui_dir/zclaw_setup_progress_model.cpp"

build_and_run zclaw_file_downloader_resume_test \
    "$test_dir/zclaw_file_downloader_resume_test.cpp" \
    "$ui_dir/zclaw_file_downloader.cpp" \
    "$ui_dir/zclaw_lhv_http_client.cpp" \
    "$ui_dir/zclaw_http_cancellation.cpp" \
    "$ui_dir/zclaw_http_client_policy.cpp" \
    "$ui_dir/zclaw_protocol.cpp"

build_and_run zclaw_selection_model_test \
    "$test_dir/zclaw_selection_model_test.cpp" \
    "$ui_dir/zclaw_selection_model.cpp"

build_and_run zclaw_settings_rules_test \
    "$test_dir/zclaw_settings_rules_test.cpp" \
    "$ui_dir/zclaw_provider_form_model.cpp" \
    "$ui_dir/zclaw_settings_navigation_model.cpp"

build_and_run zclaw_filesystem_test \
    "$test_dir/zclaw_filesystem_test.cpp" \
    "$ui_dir/zclaw_archive_installer.cpp" \
    "$ui_dir/zclaw_directory_sync.cpp" \
    "$ui_dir/zclaw_process_executor.cpp" \
    "$ui_dir/zclaw_process_runner.cpp" \
    "$ui_dir/zclaw_temporary_directory.cpp" \
    "$ui_dir/zclaw_text.cpp"

build_and_run zclaw_cli_config_model_test \
    "$test_dir/zclaw_cli_config_model_test.cpp" \
    "$ui_dir/zclaw_cli_config_model.cpp"

build_and_run zclaw_cli_output_model_test \
    "$test_dir/zclaw_cli_output_model_test.cpp" \
    "$ui_dir/zclaw_cli_output_model.cpp" \
    "$ui_dir/zclaw_text.cpp"

build_and_run zclaw_cli_service_test \
    "$test_dir/zclaw_cli_service_test.cpp" \
    "$ui_dir/zclaw_atomic_file.cpp" \
    "$ui_dir/zclaw_directory_sync.cpp" \
    "$ui_dir/zclaw_cli_config_model.cpp" \
    "$ui_dir/zclaw_cli_output_model.cpp" \
    "$ui_dir/zclaw_cli_service.cpp" \
    "$ui_dir/zclaw_path_model.cpp" \
    "$ui_dir/zclaw_paths.cpp" \
    "$ui_dir/zclaw_process_executor.cpp" \
    "$ui_dir/zclaw_process_runner.cpp" \
    "$ui_dir/zclaw_quickstart_model.cpp" \
    "$ui_dir/zclaw_risk_profile_model.cpp" \
    "$ui_dir/zclaw_risk_profile_store.cpp" \
    "$ui_dir/zclaw_text.cpp"

build_and_run zclaw_local_setup_backend_test \
    "$test_dir/zclaw_local_setup_backend_test.cpp" \
    "$ui_dir/zclaw_atomic_file.cpp" \
    "$ui_dir/zclaw_cli_config_model.cpp" \
    "$ui_dir/zclaw_cli_output_model.cpp" \
    "$ui_dir/zclaw_cli_service.cpp" \
    "$ui_dir/zclaw_directory_sync.cpp" \
    "$ui_dir/zclaw_local_cli_service.cpp" \
    "$ui_dir/zclaw_path_model.cpp" \
    "$ui_dir/zclaw_paths.cpp" \
    "$ui_dir/zclaw_process_executor.cpp" \
    "$ui_dir/zclaw_process_runner.cpp" \
    "$ui_dir/zclaw_quickstart_model.cpp" \
    "$ui_dir/zclaw_risk_profile_model.cpp" \
    "$ui_dir/zclaw_risk_profile_store.cpp" \
    "$ui_dir/zclaw_text.cpp"

build_and_run zclaw_config_codec_test \
    "$test_dir/zclaw_config_codec_test.cpp" \
    "$ui_dir/zclaw_config_codec.cpp"

build_and_run zclaw_config_document_model_test \
    "$test_dir/zclaw_config_document_model_test.cpp" \
    "$ui_dir/zclaw_config_codec.cpp" \
    "$ui_dir/zclaw_config_document_model.cpp"

build_and_run zclaw_config_manager_load_test \
    "$test_dir/zclaw_config_manager_load_test.cpp" \
    "$ui_dir/zclaw_atomic_file.cpp" \
    "$ui_dir/zclaw_directory_sync.cpp" \
    "$ui_dir/zclaw_config_codec.cpp" \
    "$ui_dir/zclaw_config_document_model.cpp" \
    "$ui_dir/zclaw_provider_catalog.cpp" \
    "$ui_dir/zclaw_provider_collection_model.cpp" \
    "$ui_dir/zclaw_provider_manager.cpp" \
    "$ui_dir/zclaw_provider_store.cpp" \
    "$ui_dir/zclaw_storage.cpp" \
    "$ui_dir/zclaw_text_file.cpp" \
    "$ui_dir/zclaw_ui_config_manager.cpp" \
    "$ui_dir/zclaw_ui_config_store.cpp"

build_and_run zclaw_risk_profile_model_test \
    "$test_dir/zclaw_risk_profile_model_test.cpp" \
    "$ui_dir/zclaw_risk_profile_model.cpp" \
    "$ui_dir/zclaw_text.cpp"

build_and_run zclaw_provider_collection_model_test \
    "$test_dir/zclaw_provider_collection_model_test.cpp" \
    "$ui_dir/zclaw_provider_collection_model.cpp"

build_and_run zclaw_runtime_model_test \
    "$test_dir/zclaw_runtime_model_test.cpp" \
    "$ui_dir/zclaw_runtime_model.cpp"

build_and_run zclaw_binary_installer_test \
    "$test_dir/zclaw_binary_installer_test.cpp" \
    "$ui_dir/zclaw_binary_installer.cpp"

build_and_run zclaw_binary_archive_cache_test \
    "$test_dir/zclaw_binary_archive_cache_test.cpp" \
    "$ui_dir/zclaw_binary_archive_cache.cpp" \
    "$ui_dir/zclaw_temporary_directory.cpp"

build_and_run zclaw_text_test \
    "$test_dir/zclaw_text_test.cpp" \
    "$ui_dir/zclaw_text.cpp"

build_and_run zclaw_text_file_test \
    "$test_dir/zclaw_text_file_test.cpp" \
    "$ui_dir/zclaw_text_file.cpp"

build_and_run zclaw_task_batch_test \
    "$test_dir/zclaw_task_batch_test.cpp" \
    "$ui_dir/zclaw_task_batch.cpp"

build_and_run zclaw_atomic_file_test \
    "$test_dir/zclaw_atomic_file_test.cpp" \
    "$ui_dir/zclaw_atomic_file.cpp" \
    "$ui_dir/zclaw_directory_sync.cpp"

build_and_run zclaw_directory_sync_test \
    "$test_dir/zclaw_directory_sync_test.cpp" \
    "$ui_dir/zclaw_directory_sync.cpp"

build_and_run zclaw_font_path_model_test \
    "$test_dir/zclaw_font_path_model_test.cpp" \
    "$ui_dir/zclaw_font_path_model.cpp"

build_and_run zclaw_path_model_test \
    "$test_dir/zclaw_path_model_test.cpp" \
    "$ui_dir/zclaw_path_model.cpp"

build_and_run zclaw_authorization_session_model_test \
    "$test_dir/zclaw_authorization_session_model_test.cpp" \
    "$ui_dir/zclaw_authorization_session_model.cpp"

build_and_run zclaw_request_id_test \
    "$test_dir/zclaw_request_id_test.cpp" \
    "$ui_dir/zclaw_request_id.cpp"

build_and_run zclaw_storage_test \
    "$test_dir/zclaw_storage_test.cpp" \
    "$ui_dir/zclaw_storage.cpp"

build_and_run zclaw_connectivity_cancel_test \
    "$test_dir/zclaw_connectivity_cancel_test.cpp" \
    "$ui_dir/zclaw_connectivity.cpp" \
    "$ui_dir/zclaw_lhv_http_client.cpp" \
    "$ui_dir/zclaw_http_client_policy.cpp" \
    "$ui_dir/zclaw_protocol.cpp"

build_and_run zclaw_lhv_http_client_test \
    "$test_dir/zclaw_lhv_http_client_test.cpp" \
    "$ui_dir/zclaw_lhv_http_client.cpp"

build_and_run zclaw_http_cancellation_test \
    "$test_dir/zclaw_http_cancellation_test.cpp" \
    "$ui_dir/zclaw_http_cancellation.cpp"

build_and_run zclaw_process_executor_test \
    "$test_dir/zclaw_process_executor_test.cpp" \
    "$ui_dir/zclaw_process_executor.cpp" \
    "$ui_dir/zclaw_process_runner.cpp" \
    "$ui_dir/zclaw_text.cpp"

build_and_run zclaw_setup_service_test \
    "$test_dir/zclaw_setup_service_test.cpp" \
    "$ui_dir/zclaw_quickstart_model.cpp" \
    "$ui_dir/zclaw_setup_service.cpp"

build_and_run zclaw_quickstart_service_test \
    "$test_dir/zclaw_quickstart_service_test.cpp" \
    "$ui_dir/zclaw_quickstart_service.cpp"

build_and_run zclaw_async_service_test \
    "$test_dir/zclaw_async_service_test.cpp" \
    "$ui_dir/zclaw_async_service.cpp" \

build_and_run zclaw_input_routing_test \
    "$test_dir/zclaw_input_routing_test.cpp" \
    "$ui_dir/zclaw_input_model.cpp" \
    "$ui_dir/zclaw_key_event_adapter.cpp" \
    "$ui_dir/zclaw_key_router.cpp"

if [ "$tests_run" -eq 0 ]; then
    echo "No tests matched ZCLAW_TEST_FILTER: $test_filter" >&2
    exit 1
fi
