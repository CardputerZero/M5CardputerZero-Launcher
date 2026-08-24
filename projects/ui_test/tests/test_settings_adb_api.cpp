#include "settings_adb_api.hpp"

#include <cassert>
#include <list>
#include <memory>
#include <string>
#include <utility>

namespace {

struct FakeBackendState {
    std::list<std::string> process_arguments;
    std::list<std::string> admin_arguments;
    settings_adb::ProcessCallback process_callback;
    settings_adb::AdminCompleteCallback admin_complete;
    settings_adb::AdminStartedCallback admin_started;
    int cancel_count = 0;
    uint64_t cancelled_id = 0;
};

settings_adb::Backend make_backend(const std::shared_ptr<FakeBackendState> &state)
{
    settings_adb::Backend backend;
    backend.process = [state](std::list<std::string> arguments,
                              settings_adb::ProcessCallback callback) {
        state->process_arguments = std::move(arguments);
        state->process_callback = std::move(callback);
    };
    backend.system_admin = [state](std::list<std::string> arguments,
                                   int,
                                   int,
                                   settings_adb::AdminCompleteCallback complete,
                                   settings_adb::AdminStartedCallback started) {
        state->admin_arguments = std::move(arguments);
        state->admin_complete = std::move(complete);
        state->admin_started = std::move(started);
    };
    backend.cancel = [state](uint64_t id, settings_adb::CancelCompleteCallback complete) {
        ++state->cancel_count;
        state->cancelled_id = id;
        if (complete) complete(0);
    };
    return backend;
}

std::string valid_key()
{
    return "QAAAA" + std::string(694, 'A') + "= host@workstation";
}

} // namespace

int main()
{
    using settings_adb::Operation;
    using settings_adb::ResultKind;

    auto state = std::make_shared<FakeBackendState>();
    settings_adb::AdbApi api(make_backend(state));

    settings_adb::Result status_result;
    assert(api.query_status([&](settings_adb::Result result) { status_result = std::move(result); }));
    assert(state->process_arguments == settings_adb::status_request());
    state->process_callback(
        0,
        "adbd=active\nenabled=enabled\nauthorizations=1\n" +
            std::string("authorization=") + std::string(64, 'a') + "\thost\n");
    assert(status_result.ok() && status_result.operation == Operation::Status);
    assert(status_result.status_valid && status_result.status.authorizations == 1);

    settings_adb::Result set_result;
    assert(api.set_enabled(false, [&](settings_adb::Result result) {
        set_result = std::move(result);
    }));
    assert(state->admin_arguments == settings_adb::set_enabled_request(false));
    state->admin_started(0, 42);
    state->admin_complete(0, 0);
    assert(set_result.ok() && set_result.operation == Operation::SetEnabled);
    assert(set_result.request_id == 42);

    settings_adb::Result exit_result;
    assert(api.set_enabled(true, [&](settings_adb::Result result) {
        exit_result = std::move(result);
    }));
    state->admin_started(0, 48);
    state->admin_complete(0, 7);
    assert(exit_result.kind == ResultKind::ExecFailed);
    assert(exit_result.exit_code == 7);

    settings_adb::Result invalid_result;
    assert(!api.authorize("invalid", [&](settings_adb::Result result) {
        invalid_result = std::move(result);
    }));
    assert(invalid_result.kind == ResultKind::InvalidRequest);

    settings_adb::Result auth_result;
    assert(api.authorize(valid_key(), [&](settings_adb::Result result) {
        auth_result = std::move(result);
    }));
    assert(state->admin_arguments == settings_adb::authorize_request(valid_key()));
    state->admin_started(0, 43);
    state->admin_complete(1, 1);
    assert(auth_result.kind == ResultKind::AuthFailed);
    assert(settings_adb::result_message(auth_result) == "Authentication failed");

    settings_adb::Result pair_result;
    assert(api.pair(valid_key(), true, [&](settings_adb::Result result) {
        pair_result = std::move(result);
    }));
    assert(state->admin_arguments == settings_adb::authorize_request(valid_key()));
    state->admin_started(0, 44);
    state->admin_complete(0, 0);
    assert(state->admin_arguments == settings_adb::set_enabled_request(true));
    state->admin_started(0, 45);
    state->admin_complete(0, 0);
    assert(pair_result.ok() && pair_result.operation == Operation::Pair);

    settings_adb::Result duplicate_start_result;
    assert(api.set_enabled(false, [&](settings_adb::Result result) {
        duplicate_start_result = std::move(result);
    }));
    state->admin_started(0, 49);
    state->admin_started(0, 50);
    assert(state->cancelled_id == 50);
    state->admin_complete(0, 0);
    assert(duplicate_start_result.ok());

    settings_adb::Result timeout_result;
    assert(api.clear_authorizations([&](settings_adb::Result result) {
        timeout_result = std::move(result);
    }));
    state->admin_started(0, 46);
    state->admin_complete(4, -110);
    assert(timeout_result.kind == ResultKind::TimedOut);

    settings_adb::Result cancelled_result;
    assert(api.set_enabled(true, [&](settings_adb::Result result) {
        cancelled_result = std::move(result);
    }));
    state->admin_started(0, 47);
    const int cancel_count_before_cancel = state->cancel_count;
    assert(api.cancel());
    assert(cancelled_result.kind == ResultKind::Cancelled);
    assert(state->cancel_count == cancel_count_before_cancel + 1 &&
           state->cancelled_id == 47);
    state->admin_complete(0, 0);
    assert(cancelled_result.kind == ResultKind::Cancelled);

    settings_adb::Result cancelled_before_start_result;
    const int cancel_count_before_start = state->cancel_count;
    assert(api.set_enabled(false, [&](settings_adb::Result result) {
        cancelled_before_start_result = std::move(result);
    }));
    assert(api.cancel());
    assert(cancelled_before_start_result.kind == ResultKind::Cancelled);
    assert(state->cancel_count == cancel_count_before_start);
    state->admin_started(0, 51);
    assert(state->cancelled_id == 51);
    state->admin_complete(0, 0);
    assert(cancelled_before_start_result.kind == ResultKind::Cancelled);

    settings_adb::Result busy_result;
    assert(api.reboot([&](settings_adb::Result result) {
        busy_result = std::move(result);
    }));
    assert(!api.set_enabled(false, [&](settings_adb::Result result) {
        busy_result = std::move(result);
    }));
    assert(busy_result.kind == ResultKind::Busy);
    state->process_callback(0, "");
    assert(!api.pending());

    settings_adb::Result reboot_result;
    assert(api.reboot([&](settings_adb::Result result) {
        reboot_result = std::move(result);
    }));
    assert(state->process_arguments == settings_adb::reboot_request());
    state->process_callback(-1, "reboot failed");
    assert(reboot_result.kind == ResultKind::ExecFailed);

    settings_adb::Result start_failure_result;
    auto start_failure_backend = make_backend(std::make_shared<FakeBackendState>());
    start_failure_backend.system_admin =
        [](std::list<std::string>, int, int, settings_adb::AdminCompleteCallback,
           settings_adb::AdminStartedCallback started) {
            started(-22, 0);
        };
    settings_adb::AdbApi start_failure_api(std::move(start_failure_backend));
    assert(!start_failure_api.set_enabled(true, [&](settings_adb::Result result) {
        start_failure_result = std::move(result);
    }));
    assert(start_failure_result.kind == ResultKind::InvalidRequest);
    assert(!start_failure_api.pending());

    settings_adb::Result shutdown_result;
    bool shutdown_callback_called = false;
    auto shutdown_state = std::make_shared<FakeBackendState>();
    settings_adb::AdbApi shutdown_api(make_backend(shutdown_state));
    assert(shutdown_api.query_status([&](settings_adb::Result result) {
        shutdown_callback_called = true;
        shutdown_result = std::move(result);
    }));
    shutdown_api.shutdown();
    shutdown_state->process_callback(0, "adbd=active\nenabled=enabled\n");
    assert(!shutdown_callback_called);
    assert(!shutdown_api.pending());
    return 0;
}
