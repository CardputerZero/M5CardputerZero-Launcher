#include "zclaw_provider_store.h"
#include "zclaw_provider_catalog.h"
#include "zclaw_provider_manager.h"
#include "zclaw_ui_config_store.h"
#include "zclaw_ui_config_manager.h"
#include "zclaw_settings_controller.h"
#include "zclaw_settings_presentation.h"
#include "zclaw_task_inbox.h"
#include "zclaw_storage.h"
#include "zclaw_paths.h"
#include "zclaw_process_runner.h"
#include "zclaw_runtime_state.h"
#include "zclaw_risk_profile_store.h"
#include "zclaw_approval_controller.h"
#include "zclaw_callback_lifetime.h"
#include "zclaw_chat_transport.h"
#include "zclaw_cli_service.h"
#include "zclaw_pairing_service.h"
#include "zclaw_lhv_http_client.h"
#include "zclaw_webhook_transport.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using zclaw::ApprovalRequest;
using zclaw::OperationResult;
using zclaw::ProviderConfig;
using zclaw::UiConfig;

namespace {

bool same_provider(const ProviderConfig &left, const ProviderConfig &right)
{
    return left.alias == right.alias && left.family == right.family &&
           left.model == right.model && left.uri == right.uri &&
           left.api_key == right.api_key && left.wire_api == right.wire_api;
}

}  // namespace

int main()
{
    char dir_template[] = "/tmp/zclaw-provider-store-XXXXXX";
    const char *dir = ::mkdtemp(dir_template);
    assert(dir);
    const std::string path = std::string(dir) + "/providers.tsv";

    const std::vector<ProviderConfig> original = {
        {"zclaw", "custom", "model\\name\nnext", "https://example.com/a\tb", "key\\value\nline"},
        {"second", "ollama", "llama3.1", "http://127.0.0.1:11434", ""},
        {"zclaw", "openai", "gpt-4.1-mini", "https://api.openai.com/v1", "openai-key"},
    };
    std::string error;
    assert(zclaw::save_provider_configs(path, original, &error));
    assert(error.empty());

    struct stat st {};
    assert(::stat(path.c_str(), &st) == 0);
    assert((st.st_mode & 0777) == 0600);

    std::vector<ProviderConfig> loaded;
    assert(zclaw::load_provider_configs(path, &loaded));
    assert(loaded.size() == original.size());
    for (size_t i = 0; i < original.size(); ++i)
        assert(same_provider(loaded[i], original[i]));

    const std::string ui_path = std::string(dir) + "/ui.tsv";
    UiConfig ui_original;
    ui_original.webhook_url = "https://example.com/hook\tpath";
    ui_original.agent_alias = "test\nagent";
    ui_original.webhook_secret = "hook\\secret";
    ui_original.bearer_token = "bearer-token";
    ui_original.setup_complete = true;
    assert(zclaw::save_ui_config(ui_path, ui_original, &error));
    struct stat ui_st {};
    assert(::stat(ui_path.c_str(), &ui_st) == 0);
    assert((ui_st.st_mode & 0777) == 0600);
    UiConfig ui_loaded;
    assert(zclaw::load_ui_config(ui_path, &ui_loaded));
    assert(ui_loaded.webhook_url == ui_original.webhook_url);
    assert(ui_loaded.agent_alias == ui_original.agent_alias);
    assert(ui_loaded.webhook_secret == ui_original.webhook_secret);
    assert(ui_loaded.bearer_token == ui_original.bearer_token);
    assert(ui_loaded.setup_complete == ui_original.setup_complete);

    assert(zclaw::provider_preset_count() == 6);
    const std::vector<ProviderConfig> defaults = zclaw::default_provider_configs();
    assert(defaults.size() == zclaw::provider_preset_count());
    assert(defaults.front().family == "openai");
    assert(defaults.back().family == "custom");
    assert(std::string(zclaw::provider_display_name("anthropic")) == "Anthropic");
    assert(std::string(zclaw::provider_display_name("unknown")) == "Custom");
    assert(zclaw::provider_preset_index("ollama") == 3);
    assert(zclaw::provider_preset_index("unknown") == 5);
    assert(zclaw::provider_preset(999).family == "custom");

    ProviderConfig custom = zclaw::provider_preset(5);
    custom.api_key = "key";
    custom.uri = "https://example.com/v1";
    custom.model = "model";

    ProviderConfig ollama = zclaw::provider_preset(3);
    using zclaw::SettingsView;
    using zclaw::SettingsActivationAction;
    using zclaw::SettingsBackAction;

    zclaw::SettingsController settings_controller;
    assert(settings_controller.view() == SettingsView::Main);
    settings_controller.reset_view(SettingsView::Providers);
    settings_controller.move_providers(8, 5, 5);
    assert(settings_controller.provider_selection().selected_index == 5);
    assert(settings_controller.provider_selection().scroll_offset == 1);
    assert(settings_controller.selected_row() == 4);
    settings_controller.open_provider_detail(4);
    assert(settings_controller.provider_detail_index() == 4);
    assert(settings_controller.selected_row() == 0);
    settings_controller.close_provider_detail();
    assert(settings_controller.provider_detail_index() == -1);
    settings_controller.set_selected_row(2);
    settings_controller.sync_setup_provider(5, 6, 5);
    assert(settings_controller.setup_provider_selection().selected_index == 5);
    assert(settings_controller.setup_provider_selection().scroll_offset == 1);
    assert(settings_controller.selected_row() == 2);
    settings_controller.normalize_setup_providers(6, 5);
    assert(settings_controller.selected_row() == 4);
    settings_controller.move_setup_providers(6, 5, -1);
    assert(settings_controller.setup_provider_selection().selected_index == 4);
    settings_controller.reset_view(SettingsView::Setup, 2);
    assert(settings_controller.activation(2, 3, false).action ==
           SettingsActivationAction::StartSetup);
    assert(settings_controller.back_action(true, false) == SettingsBackAction::None);
    settings_controller.set_provider_edit_field(zclaw::ProviderEditField::Model);
    assert(settings_controller.provider_edit_field() == zclaw::ProviderEditField::Model);
    settings_controller.set_setup_edit_field(zclaw::SetupEditField::ApiKey);
    assert(settings_controller.setup_edit_field() == zclaw::SetupEditField::ApiKey);

    UiConfig presentation_config;
    presentation_config.agent_alias = "demo-agent";
    zclaw::SettingsPresentation settings_presentation =
        zclaw::present_settings_main(presentation_config);
    assert(settings_presentation.title == "ZClaw Settings");
    assert(settings_presentation.rows.size() == 5);
    assert(settings_presentation.rows[0].value == "Run");
    assert(settings_presentation.rows[3].value == "demo-agent");
    assert(settings_presentation.rows[4].value == "Webhook");
    presentation_config.setup_complete = true;
    presentation_config.bearer_token = "token";
    settings_presentation = zclaw::present_authorization(presentation_config);
    assert(settings_presentation.rows[1].value == "Saved");
    assert(settings_presentation.rows[3].value == "WS");

    settings_presentation = zclaw::present_setup(custom);
    assert(settings_presentation.rows.size() == 6);
    assert(settings_presentation.rows[0].value == "Custom");
    assert(settings_presentation.rows[4].title == "API Type");
    assert(settings_presentation.rows[4].value == "completions");
    assert(settings_presentation.rows[5].title == "Confirm");
    settings_presentation = zclaw::present_setup(ollama);
    assert(settings_presentation.rows.size() == 2);

    zclaw::PagedSelection presentation_selection{5, 1, 4};
    settings_presentation =
        zclaw::present_setup_providers(presentation_selection, 5);
    assert(settings_presentation.rows.size() == 5);
    assert(settings_presentation.rows.back().value == "Selected");
    settings_presentation =
        zclaw::present_providers(original, zclaw::PagedSelection{2, 1, 1}, 2);
    assert(settings_presentation.rows.size() == 2);
    assert(settings_presentation.rows[0].title == original[0].alias);
    assert(settings_presentation.rows[1].title == original[1].alias);
    settings_presentation = zclaw::present_provider_detail(original[0]);
    assert(settings_presentation.rows.size() == 6);
    assert(settings_presentation.rows[4].value == "set");
    assert(settings_presentation.rows[5].title == "API Type");
    assert(zclaw::present_setup_progress().rows.empty());

    assert(::setenv("HOME", dir, 1) == 0);
    assert(zclaw::paths::home_dir() == dir);
    assert(zclaw::paths::zeroclaw_dir() == std::string(dir) + "/.zeroclaw");
    assert(zclaw::paths::providers_config() ==
           std::string(dir) + "/.zeroclaw/zclaw_providers.tsv");
    UiConfig runtime_config;
    assert(zclaw::RuntimeState::first_run_needed(runtime_config));
    const std::string runtime_directory = zclaw::paths::zeroclaw_dir();
    const std::string runtime_config_path = zclaw::paths::zeroclaw_config();
    assert(::mkdir(runtime_directory.c_str(), 0700) == 0);
    assert(zclaw::RuntimeState::first_run_needed(runtime_config));
    {
        std::ofstream runtime_file(runtime_config_path);
        assert(runtime_file);
        runtime_file << "# test config\n";
    }
    assert(zclaw::RuntimeState::first_run_needed(runtime_config));
    runtime_config.setup_complete = true;
    assert(zclaw::RuntimeState::first_run_needed(runtime_config));
    runtime_config.bearer_token = "token";
    assert(!zclaw::RuntimeState::first_run_needed(runtime_config));

    zclaw::ApprovalController approvals;
    ApprovalRequest approval_request;
    approval_request.request_id = "request-1";
    assert(approvals.begin(approval_request));
    assert(approvals.pending());
    assert(approvals.pending_request("request-1"));
    assert(!approvals.pending_request("request-2"));
    assert(approvals.selected_decision() == "approve");
    approvals.move_selection(-1);
    assert(approvals.selected_index() == 2);
    assert(approvals.selected_decision() == "deny");
    std::thread answer_thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(approvals.answer("always"));
    });
    const zclaw::ApprovalWaitResult approval_result =
        approvals.wait_for(approval_request.request_id, std::chrono::seconds(1));
    answer_thread.join();
    assert(!approval_result.timed_out);
    assert(approval_result.decision == "always");
    assert(!approvals.pending());
    assert(!approvals.answer("approve"));

    assert(approvals.begin(approval_request));
    const zclaw::ApprovalWaitResult timeout_result =
        approvals.wait_for(approval_request.request_id, std::chrono::milliseconds(1));
    assert(timeout_result.timed_out);
    assert(timeout_result.decision == "deny");

    ApprovalRequest concurrent_request;
    concurrent_request.request_id = "request-2";
    zclaw::ApprovalController concurrent_approvals;
    std::promise<void> begin_gate;
    std::shared_future<void> begin_ready = begin_gate.get_future().share();
    std::future<bool> first_begin = std::async(std::launch::async, [&] {
        begin_ready.wait();
        return concurrent_approvals.begin(approval_request);
    });
    std::future<bool> second_begin = std::async(std::launch::async, [&] {
        begin_ready.wait();
        return concurrent_approvals.begin(concurrent_request);
    });
    begin_gate.set_value();
    const bool first_accepted = first_begin.get();
    const bool second_accepted = second_begin.get();
    assert(first_accepted != second_accepted);
    concurrent_approvals.cancel();

    assert(approvals.begin(approval_request));
    assert(!approvals.begin(concurrent_request));
    std::promise<zclaw::ApprovalWaitResult> primary_wait;
    std::future<zclaw::ApprovalWaitResult> primary_result = primary_wait.get_future();
    std::thread primary_waiter([&] {
        primary_wait.set_value(
            approvals.wait_for(approval_request.request_id, std::chrono::seconds(1)));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const zclaw::ApprovalWaitResult duplicate_wait =
        approvals.wait_for(approval_request.request_id, std::chrono::milliseconds(1));
    assert(!duplicate_wait.timed_out);
    assert(duplicate_wait.decision == "deny");
    const zclaw::ApprovalWaitResult rejected_wait =
        approvals.wait_for(concurrent_request.request_id, std::chrono::milliseconds(1));
    assert(!rejected_wait.timed_out);
    assert(rejected_wait.decision == "deny");
    assert(approvals.answer("approve"));
    primary_waiter.join();
    assert(primary_result.get().decision == "approve");
    assert(!approvals.pending());

    assert(approvals.begin(concurrent_request));
    std::future<zclaw::ApprovalWaitResult> cancelled_result =
        std::async(std::launch::async, [&] {
            return approvals.wait_for(concurrent_request.request_id,
                                      std::chrono::seconds(1));
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    approvals.cancel();
    assert(cancelled_result.get().decision == "deny");
    assert(!approvals.pending());
    assert(approvals.begin(approval_request));
    approvals.cancel();

    assert(approvals.begin(approval_request));
    std::future<zclaw::ApprovalWaitResult> shutdown_result =
        std::async(std::launch::async, [&] {
            return approvals.wait_for(approval_request.request_id,
                                      std::chrono::seconds(1));
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    approvals.shutdown();
    assert(shutdown_result.get().decision == "deny");
    assert(!approvals.begin(approval_request));
    assert(!approvals.pending());

    struct CallbackTarget {
        int calls = 0;
    } callback_target;
    auto callback_lifetime =
        std::make_shared<zclaw::CallbackLifetime<CallbackTarget>>(&callback_target);
    std::function<void()> queued_callback = [callback_lifetime] {
        callback_lifetime->invoke([](CallbackTarget &target) { ++target.calls; });
    };
    queued_callback();
    assert(callback_target.calls == 1);
    callback_lifetime->invalidate();
    queued_callback();
    assert(callback_target.calls == 1);

    CallbackTarget concurrent_target;
    auto concurrent_lifetime =
        std::make_shared<zclaw::CallbackLifetime<CallbackTarget>>(&concurrent_target);
    std::promise<void> callback_entered;
    std::promise<void> release_callback;
    std::shared_future<void> callback_release = release_callback.get_future().share();
    std::future<bool> active_callback = std::async(std::launch::async, [&] {
        return concurrent_lifetime->invoke([&](CallbackTarget &target) {
            callback_entered.set_value();
            callback_release.wait();
            ++target.calls;
        });
    });
    callback_entered.get_future().wait();
    std::future<void> invalidation = std::async(std::launch::async, [&] {
        concurrent_lifetime->invalidate();
    });
    assert(invalidation.wait_for(std::chrono::milliseconds(5)) ==
           std::future_status::timeout);
    release_callback.set_value();
    assert(active_callback.get());
    invalidation.get();
    assert(concurrent_target.calls == 1);
    assert(!concurrent_lifetime->invoke(
        [](CallbackTarget &target) { ++target.calls; }));

    zclaw::TaskInbox task_inbox;
    assert(!task_inbox.active());
    assert(!task_inbox.post([] {}));
    task_inbox.activate();
    std::vector<std::thread> task_posters;
    for (int thread_index = 0; thread_index < 4; ++thread_index) {
        task_posters.emplace_back([&task_inbox] {
            for (int task_index = 0; task_index < 25; ++task_index)
                assert(task_inbox.post([] {}));
        });
    }
    for (std::thread &thread : task_posters)
        thread.join();
    std::vector<zclaw::TaskInbox::Task> pending_tasks = task_inbox.take_pending();
    assert(pending_tasks.size() == 100);
    task_inbox.post([] { assert(false); });
    task_inbox.shutdown();
    assert(!task_inbox.active());
    assert(task_inbox.take_pending().empty());
    assert(!task_inbox.post([] {}));

    const std::vector<ProviderConfig> replacement = {
        {"zclaw", "openai", "gpt-4.1-mini", "https://api.openai.com/v1", "new-key"},
    };
    assert(zclaw::save_provider_configs(path, replacement, &error));
    assert(zclaw::load_provider_configs(path, &loaded));
    assert(loaded.size() == 1 && same_provider(loaded[0], replacement[0]));

    {
        std::ofstream legacy(path, std::ios::trunc);
        assert(legacy);
        legacy << "legacy\tcustom\tmodel\\tname\thttps://example.com/v1\tkey\\\\value\n";
    }
    assert(zclaw::load_provider_configs(path, &loaded));
    assert(loaded.size() == 1);
    assert(loaded[0].alias == "legacy");
    assert(loaded[0].model == "model\tname");
    assert(loaded[0].api_key == "key\\value");

    const std::string blocked_parent = std::string(dir) + "/blocked-parent";
    {
        std::ofstream blocking_file(blocked_parent);
        assert(blocking_file);
    }
    assert(!zclaw::save_provider_configs(blocked_parent + "/providers.tsv",
                                         original, &error));
    assert(!error.empty());
    assert(zclaw::load_provider_configs(path, &loaded));
    assert(loaded.size() == 1 && loaded[0].alias == "legacy");

    const std::string manager_path = std::string(dir) + "/managed.tsv";
    zclaw::ProviderManager provider_manager(manager_path);
    provider_manager.load();
    assert(provider_manager.providers().size() == zclaw::provider_preset_count());
    ProviderConfig managed_provider = {
        "managed", "openai-compatible", "managed-model", "https://managed.example/v1", "key"
    };
    std::size_t managed_index = 0;
    assert(provider_manager.add(managed_provider, &managed_index, &error));
    assert(managed_index == zclaw::provider_preset_count());
    managed_provider.model = "updated-model";
    assert(provider_manager.replace(managed_index, managed_provider, &error));
    assert(provider_manager.providers()[managed_index].model == "updated-model");
    ProviderConfig managed_setup = provider_manager.setup_provider();
    managed_setup.api_key = "setup-key";
    assert(provider_manager.update_setup_provider(managed_setup, &error));
    assert(provider_manager.setup_provider().api_key == "setup-key");
    const ProviderConfig custom_preset = zclaw::provider_preset(5);
    assert(provider_manager.activate(custom_preset, &error));
    assert(provider_manager.setup_provider().family == "custom");
    assert(provider_manager.erase(managed_index, zclaw::provider_preset(0), &error));

    zclaw::ProviderManager failing_manager(
        blocked_parent + "/deeper/providers.tsv");
    failing_manager.load();
    const std::vector<ProviderConfig> before_failed_add = failing_manager.providers();
    assert(!failing_manager.add(managed_provider, nullptr, &error));
    assert(failing_manager.providers().size() == before_failed_add.size());
    assert(failing_manager.setup_provider().family == before_failed_add.front().family);

    const std::string managed_ui_path = std::string(dir) + "/managed-ui.tsv";
    zclaw::UiConfigManager ui_config_manager(managed_ui_path);
    ui_config_manager.load();
    UiConfig managed_ui = ui_config_manager.config();
    managed_ui.agent_alias = "managed-agent";
    managed_ui.bearer_token = "managed-token";
    managed_ui.setup_complete = true;
    assert(ui_config_manager.update(managed_ui, &error));
    assert(ui_config_manager.config().agent_alias == "managed-agent");
    assert(ui_config_manager.clear_token(&error));
    assert(ui_config_manager.config().bearer_token.empty());
    zclaw::UiConfigManager reloaded_ui_config(managed_ui_path);
    reloaded_ui_config.load();
    assert(reloaded_ui_config.config().agent_alias == "managed-agent");
    assert(reloaded_ui_config.config().setup_complete);

    zclaw::UiConfigManager failing_ui_config(
        blocked_parent + "/deeper/ui.tsv");
    UiConfig failed_candidate = failing_ui_config.config();
    failed_candidate.agent_alias = "must-not-commit";
    assert(!failing_ui_config.update(failed_candidate, &error));
    assert(failing_ui_config.config().agent_alias == "zclaw");

    const std::string non_directory = std::string(dir) + "/not-a-directory";
    {
        std::ofstream file(non_directory);
        assert(file);
    }
    assert(!zclaw::ensure_private_parent_directory(
        non_directory + "/config.tsv", &error));
    assert(!error.empty());

    assert(zclaw::ProcessRunner::shell_quote("") == "''");
    assert(zclaw::ProcessRunner::shell_quote("plain value") == "'plain value'");
    assert(zclaw::ProcessRunner::shell_quote("it's") == "'it'\\''s'");
    const zclaw::CommandResult command_result =
        zclaw::ProcessRunner::run_shell("printf '  captured output  \\n'");
    assert(command_result.ok());
    assert(command_result.output == "captured output");
    assert(!zclaw::ProcessRunner::run_shell("false").ok());
    error.clear();
    assert(!zclaw::CliService().apply_config(nullptr, ProviderConfig{}, &error));
    assert(error == "Missing UI configuration.");

    const std::string risk_path = std::string(dir) + "/zeroclaw.toml";
    const std::string expected_risk =
        "[gateway]\nport = 42617\n\n[risk_profiles.default]\n";
    {
        std::ofstream risk_file(risk_path, std::ios::binary);
        assert(risk_file);
        risk_file << "[gateway]\nport = 42617";
    }
    assert(::chmod(risk_path.c_str(), 0640) == 0);
    assert(zclaw::RiskProfileStore().ensure_default(risk_path, &error));
    struct stat risk_state {};
    assert(::stat(risk_path.c_str(), &risk_state) == 0);
    assert((risk_state.st_mode & 0777) == 0640);
    std::ifstream stored_risk_file(risk_path, std::ios::binary);
    const std::string stored_risk(
        (std::istreambuf_iterator<char>(stored_risk_file)),
        std::istreambuf_iterator<char>());
    assert(stored_risk == expected_risk);
    assert(zclaw::RiskProfileStore().ensure_default(risk_path, &error));

    UiConfig invalid_transport_config;
    invalid_transport_config.webhook_url = "not-a-url";
    auto invalid_http_client = std::make_shared<zclaw::LhvHttpClient>();
    OperationResult transport_result;
    assert(zclaw::WebhookTransport(invalid_http_client).send(
        invalid_transport_config, "hello",
        [&transport_result](OperationResult result) {
            transport_result = std::move(result);
        }));
    assert(!transport_result.ok);
    assert(transport_result.text == "Webhook request failed.\nInvalid URL.");
    assert(zclaw::PairingService(invalid_http_client).pair(
        invalid_transport_config, "1234",
        [&transport_result](OperationResult result) {
            transport_result = std::move(result);
        }));
    assert(!transport_result.ok);
    assert(transport_result.text == "Pairing request failed.\nInvalid URL.");
    assert(zclaw::WebSocketTransport().send(
        invalid_transport_config, "hello", {},
        [&transport_result](OperationResult result) {
            transport_result = std::move(result);
        }));
    assert(!transport_result.ok);
    assert(transport_result.text == "WS chat failed.\nInvalid WebSocket URL.");

    assert(::unlink(path.c_str()) == 0);
    assert(::unlink(ui_path.c_str()) == 0);
    assert(::unlink(manager_path.c_str()) == 0);
    assert(::unlink(managed_ui_path.c_str()) == 0);
    assert(::unlink(blocked_parent.c_str()) == 0);
    assert(::unlink(non_directory.c_str()) == 0);
    assert(::unlink(runtime_config_path.c_str()) == 0);
    assert(::unlink(risk_path.c_str()) == 0);
    assert(::rmdir(runtime_directory.c_str()) == 0);
    assert(::rmdir(dir) == 0);
    return 0;
}
