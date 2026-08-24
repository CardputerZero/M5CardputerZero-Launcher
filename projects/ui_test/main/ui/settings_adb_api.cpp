#include "settings_adb_api.hpp"

#ifndef SETTINGS_ADB_API_NO_CP0
#include "hal_lvgl_bsp.h"
#endif

#include <cerrno>
#include <exception>
#include <mutex>
#include <utility>

namespace settings_adb {

struct AdbApi::ActiveRequest {
    Operation operation = Operation::Status;
    Completion completion;
    uint64_t generation = 0;
    std::atomic_bool cancel_requested{false};
    std::atomic_bool started_seen{false};
    std::atomic<int> start_code{0};
    std::atomic<uint64_t> request_id{0};
};

struct AdbApi::State {
    mutable std::mutex mutex;
    Backend backend;
    bool accepting = true;
    uint64_t generation = 1;
    std::shared_ptr<ActiveRequest> active;
};

namespace {

void invoke_completion(Completion completion, Result result) noexcept
{
    if (!completion) return;
    try {
        completion(std::move(result));
    } catch (...) {
    }
}

Result make_result(Operation operation, ResultKind kind, int code)
{
    Result result;
    result.operation = operation;
    result.kind = kind;
    result.code = code;
    return result;
}

ResultKind process_result_kind(int code)
{
    return code == 0 ? ResultKind::Success : ResultKind::ExecFailed;
}

ResultKind admin_result_kind(int result_code, int exit_code)
{
    const auto kind = setting::classify_privileged_result(result_code);
    if (kind == setting::PrivilegedResultKind::SUCCESS && exit_code != 0)
        return ResultKind::ExecFailed;

    switch (kind) {
    case setting::PrivilegedResultKind::SUCCESS: return ResultKind::Success;
    case setting::PrivilegedResultKind::AUTH_FAILED: return ResultKind::AuthFailed;
    case setting::PrivilegedResultKind::CANCELLED: return ResultKind::Cancelled;
    case setting::PrivilegedResultKind::TIMED_OUT: return ResultKind::TimedOut;
    case setting::PrivilegedResultKind::EXEC_FAILED: return ResultKind::ExecFailed;
    }
    return ResultKind::ExecFailed;
}

ResultKind start_result_kind(int code)
{
    return code == -EINVAL ? ResultKind::InvalidRequest : ResultKind::BackendUnavailable;
}

bool is_current(const std::shared_ptr<AdbApi::State> &state,
                const std::shared_ptr<AdbApi::ActiveRequest> &request)
{
    if (!state || !request) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->accepting && state->active == request &&
           request->generation == state->generation;
}

std::shared_ptr<AdbApi::ActiveRequest> begin_request(
    const std::shared_ptr<AdbApi::State> &state,
    Operation operation,
    Completion completion)
{
    if (!state) return {};
    try {
        auto request = std::make_shared<AdbApi::ActiveRequest>();
        request->operation = operation;
        request->completion = std::move(completion);
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting || state->active) return {};
        ++state->generation;
        if (state->generation == 0) state->generation = 1;
        request->generation = state->generation;
        state->active = request;
        return request;
    } catch (...) {
        return {};
    }
}

void finish_request(const std::shared_ptr<AdbApi::State> &state,
                    const std::shared_ptr<AdbApi::ActiveRequest> &request,
                    Result result)
{
    if (!state || !request) return;
    Completion completion;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting || state->active != request ||
            request->generation != state->generation ||
            request->cancel_requested.load(std::memory_order_acquire))
            return;
        state->active.reset();
        completion = std::move(request->completion);
    }
    invoke_completion(std::move(completion), std::move(result));
}

void finish_start_failure(const std::shared_ptr<AdbApi::State> &state,
                          const std::shared_ptr<AdbApi::ActiveRequest> &request,
                          int code)
{
    Result result = make_result(request ? request->operation : Operation::Status,
                                start_result_kind(code), code);
    result.exit_code = code;
    finish_request(state, request, std::move(result));
}

void cancel_backend(const Backend &backend, uint64_t request_id) noexcept
{
    if (!request_id || !backend.cancel) return;
    try {
        backend.cancel(request_id, {});
    } catch (...) {
    }
}

void handle_process_completion(const std::shared_ptr<AdbApi::State> &state,
                               const std::shared_ptr<AdbApi::ActiveRequest> &request,
                               int code,
                               std::string data) noexcept
{
    try {
        if (!is_current(state, request)) return;
        Result result = make_result(request->operation, process_result_kind(code), code);
        result.exit_code = code;
        result.data = std::move(data);
        if (request->operation == Operation::Status && code == 0) {
            result.status = setting::parse_adb_status(result.data);
            result.status_valid = result.status.valid && result.status.payload_valid;
            if (!result.status_valid) result.kind = ResultKind::InvalidPayload;
        }
        finish_request(state, request, std::move(result));
    } catch (...) {
        Result result = make_result(request ? request->operation : Operation::Status,
                                    ResultKind::BackendUnavailable, -ENOMEM);
        result.exit_code = -ENOMEM;
        finish_request(state, request, std::move(result));
    }
}

void handle_admin_started(const std::shared_ptr<AdbApi::State> &state,
                          const std::shared_ptr<AdbApi::ActiveRequest> &request,
                          int code,
                          uint64_t request_id)
{
    if (!state || !request) return;
    if (request->started_seen.exchange(true, std::memory_order_acq_rel)) {
        cancel_backend(state->backend, request_id);
        return;
    }
    request->start_code.store(code == 0 && request_id != 0 ? 0 : (code == 0 ? -EIO : code),
                              std::memory_order_release);
    request->request_id.store(request_id, std::memory_order_release);
    if (request->cancel_requested.load(std::memory_order_acquire)) {
        cancel_backend(state->backend, request_id);
        return;
    }
    if (code == 0 && request_id != 0) {
        if (is_current(state, request)) return;
        cancel_backend(state->backend, request_id);
        return;
    }
    cancel_backend(state->backend, request_id);
    finish_start_failure(state, request, code == 0 ? -EIO : code);
}

void handle_admin_completion(const std::shared_ptr<AdbApi::State> &state,
                             const std::shared_ptr<AdbApi::ActiveRequest> &request,
                             int result_code,
                             int exit_code)
{
    if (!is_current(state, request)) return;
    Result result = make_result(request->operation,
                                admin_result_kind(result_code, exit_code),
                                result_code);
    result.exit_code = exit_code;
    result.request_id = request->request_id.load(std::memory_order_acquire);
    finish_request(state, request, std::move(result));
}

} // namespace

Backend system_backend()
{
    Backend backend;
#ifndef SETTINGS_ADB_API_NO_CP0
    backend.process = [](std::list<std::string> arguments, ProcessCallback callback) {
        cp0_signal_process_api(std::move(arguments), std::move(callback));
    };
    backend.system_admin = [](std::list<std::string> arguments,
                              int auth_timeout_ms,
                              int exec_timeout_ms,
                              AdminCompleteCallback complete,
                              AdminStartedCallback started) {
        cp0_signal_system_admin_async(std::move(arguments), auth_timeout_ms, exec_timeout_ms,
                                      std::move(complete), std::move(started));
    };
    backend.cancel = [](uint64_t request_id, CancelCompleteCallback complete) {
        cp0_signal_sudo_cancel(request_id, std::move(complete));
    };
#endif
    return backend;
}

std::list<std::string> status_request()
{
    return {"AdbStatus"};
}

std::list<std::string> set_enabled_request(bool enabled)
{
    return {"AdbSet", enabled ? "1" : "0"};
}

std::list<std::string> authorize_request(const std::string &public_key)
{
    return {"AdbAuthorize", public_key};
}

std::list<std::string> revoke_request(const std::string &fingerprint)
{
    return {"AdbRevoke", fingerprint};
}

std::list<std::string> clear_authorizations_request()
{
    return {"AdbClearAuthorizations"};
}

std::list<std::string> reboot_request()
{
    return {"Reboot"};
}

const char *result_kind_name(ResultKind kind) noexcept
{
    switch (kind) {
    case ResultKind::Success: return "success";
    case ResultKind::InvalidRequest: return "invalid-request";
    case ResultKind::InvalidPayload: return "invalid-payload";
    case ResultKind::Busy: return "busy";
    case ResultKind::AuthFailed: return "auth-failed";
    case ResultKind::ExecFailed: return "exec-failed";
    case ResultKind::Cancelled: return "cancelled";
    case ResultKind::TimedOut: return "timed-out";
    case ResultKind::BackendUnavailable: return "backend-unavailable";
    }
    return "unknown";
}

std::string result_message(const Result &result)
{
    switch (result.kind) {
    case ResultKind::Success: return {};
    case ResultKind::InvalidRequest: return "Invalid ADB request";
    case ResultKind::InvalidPayload: return "Invalid ADB status response";
    case ResultKind::Busy: return "ADB operation already in progress";
    case ResultKind::AuthFailed: return "Authentication failed";
    case ResultKind::Cancelled: return "Request cancelled";
    case ResultKind::TimedOut: return "Request timed out";
    case ResultKind::BackendUnavailable: return "ADB backend unavailable";
    case ResultKind::ExecFailed:
        return result.exit_code < 0 ? "Unable to start command" : "Command returned an error";
    }
    return "ADB operation failed";
}

AdbApi::AdbApi() : AdbApi(system_backend())
{
}

AdbApi::AdbApi(Backend backend) : state_(std::make_shared<State>())
{
    state_->backend = std::move(backend);
}

AdbApi::~AdbApi()
{
    shutdown();
}

void AdbApi::deliver_busy(Operation operation, Completion completion)
{
    invoke_completion(std::move(completion),
                      make_result(operation, ResultKind::Busy, -EBUSY));
}

void AdbApi::deliver_invalid(Operation operation, Completion completion, int code)
{
    invoke_completion(std::move(completion),
                      make_result(operation, ResultKind::InvalidRequest, code));
}

bool AdbApi::start_process(const std::shared_ptr<State> &state,
                           Operation operation,
                           Completion completion,
                           std::list<std::string> arguments)
{
    Completion rejected_completion = completion;
    auto request = begin_request(state, operation, std::move(completion));
    if (!request) {
        deliver_busy(operation, std::move(rejected_completion));
        return false;
    }
    if (!state->backend.process) {
        finish_start_failure(state, request, -ENOSYS);
        return false;
    }

    const std::weak_ptr<State> weak_state = state;
    try {
        state->backend.process(
            std::move(arguments),
            [weak_state, request](int code, std::string data) mutable {
                const auto state = weak_state.lock();
                if (!state) return;
                handle_process_completion(state, request, code, std::move(data));
            });
    } catch (...) {
        finish_start_failure(state, request, -EIO);
        return false;
    }
    return true;
}

bool AdbApi::start_admin(const std::shared_ptr<State> &state,
                         Operation operation,
                         Completion completion,
                         std::list<std::string> arguments,
                         int auth_timeout_ms,
                         int exec_timeout_ms)
{
    Completion rejected_completion = completion;
    auto request = begin_request(state, operation, std::move(completion));
    if (!request) {
        deliver_busy(operation, std::move(rejected_completion));
        return false;
    }
    if (!state->backend.system_admin) {
        finish_start_failure(state, request, -ENOSYS);
        return false;
    }

    const std::weak_ptr<State> weak_state = state;
    try {
        state->backend.system_admin(
            std::move(arguments), auth_timeout_ms, exec_timeout_ms,
            [weak_state, request](int result_code, int exit_code) {
                const auto state = weak_state.lock();
                if (!state) return;
                handle_admin_completion(state, request, result_code, exit_code);
            },
            [weak_state, request](int code, uint64_t request_id) {
                const auto state = weak_state.lock();
                if (!state) return;
                handle_admin_started(state, request, code, request_id);
            });
    } catch (...) {
        cancel_backend(state->backend, request->request_id.load(std::memory_order_acquire));
        finish_start_failure(state, request, -EIO);
        return false;
    }
    return !request->started_seen.load(std::memory_order_acquire) ||
           request->start_code.load(std::memory_order_acquire) == 0;
}

bool AdbApi::start_pair_enable(const std::shared_ptr<State> &state,
                               Completion completion)
{
    return start_admin(state, Operation::Pair, std::move(completion),
                       set_enabled_request(true), 60000, 300000);
}

bool AdbApi::query_status(Completion completion)
{
    return start_process(state_, Operation::Status, std::move(completion), status_request());
}

bool AdbApi::set_enabled(bool enabled, Completion completion)
{
    return start_admin(state_, Operation::SetEnabled, std::move(completion),
                       set_enabled_request(enabled), 60000, 300000);
}

bool AdbApi::authorize(const std::string &public_key, Completion completion)
{
    if (!setting::adb_public_key_valid(public_key)) {
        deliver_invalid(Operation::Authorize, std::move(completion));
        return false;
    }
    return start_admin(state_, Operation::Authorize, std::move(completion),
                       authorize_request(public_key), 60000, 30000);
}

bool AdbApi::revoke(const std::string &fingerprint, Completion completion)
{
    if (!setting::adb_fingerprint_valid(fingerprint)) {
        deliver_invalid(Operation::Revoke, std::move(completion));
        return false;
    }
    return start_admin(state_, Operation::Revoke, std::move(completion),
                       revoke_request(fingerprint), 60000, 30000);
}

bool AdbApi::clear_authorizations(Completion completion)
{
    return start_admin(state_, Operation::ClearAuthorizations, std::move(completion),
                       clear_authorizations_request(), 60000, 30000);
}

bool AdbApi::pair(const std::string &public_key,
                  bool enable_after_pair,
                  Completion completion)
{
    if (!setting::adb_public_key_valid(public_key)) {
        deliver_invalid(Operation::Pair, std::move(completion));
        return false;
    }

    const std::shared_ptr<State> state = state_;
    Completion final_completion = std::move(completion);
    Completion authorize_completion =
        [state, enable_after_pair, final_completion = std::move(final_completion)](
            Result result) mutable {
            result.operation = Operation::Pair;
            if (!result.ok() || !enable_after_pair) {
                invoke_completion(std::move(final_completion), std::move(result));
                return;
            }
            const bool started = AdbApi::start_pair_enable(
                state,
                [final_completion = std::move(final_completion)](Result enable_result) mutable {
                    enable_result.operation = Operation::Pair;
                    invoke_completion(std::move(final_completion), std::move(enable_result));
                });
            if (!started) return;
        };
    return start_admin(state_, Operation::Pair, std::move(authorize_completion),
                       authorize_request(public_key), 60000, 30000);
}

bool AdbApi::reboot(Completion completion)
{
    return start_process(state_, Operation::Reboot, std::move(completion), reboot_request());
}

bool AdbApi::cancel()
{
    const std::shared_ptr<State> state = state_;
    if (!state) return false;

    std::shared_ptr<ActiveRequest> request;
    Completion completion;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        request = state->active;
        if (!request) return false;
        request->cancel_requested.store(true, std::memory_order_release);
        state->active.reset();
        ++state->generation;
        if (state->generation == 0) state->generation = 1;
        completion = std::move(request->completion);
    }

    cancel_backend(state->backend, request->request_id.load(std::memory_order_acquire));
    Result result = make_result(request->operation, ResultKind::Cancelled, 3);
    result.request_id = request->request_id.load(std::memory_order_acquire);
    invoke_completion(std::move(completion), std::move(result));
    return true;
}

void AdbApi::shutdown() noexcept
{
    const std::shared_ptr<State> state = state_;
    if (!state) return;

    std::shared_ptr<ActiveRequest> request;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting) return;
        state->accepting = false;
        request = state->active;
        state->active.reset();
        ++state->generation;
        if (state->generation == 0) state->generation = 1;
        if (request) {
            request->cancel_requested.store(true, std::memory_order_release);
            request->completion = nullptr;
        }
    }
    if (request)
        cancel_backend(state->backend, request->request_id.load(std::memory_order_acquire));
}

bool AdbApi::pending() const noexcept
{
    const std::shared_ptr<State> state = state_;
    if (!state) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->accepting && static_cast<bool>(state->active);
}

} // namespace settings_adb
