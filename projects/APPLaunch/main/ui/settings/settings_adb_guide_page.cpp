#include "settings_adb_guide_page.hpp"

#include "settings_adb_state.hpp"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

namespace settings_adb {

struct AdbApiState;

using AdbStatus = setting::AdbStatus;
using AdbAuthorization = setting::AdbAuthorization;

enum class Operation : uint8_t {
    Status,
    SetEnabled,
    Authorize,
    Revoke,
    ClearAuthorizations,
    Pair,
    Reboot,
};

enum class ResultKind : uint8_t {
    Success,
    InvalidRequest,
    InvalidPayload,
    Busy,
    AuthFailed,
    ExecFailed,
    Cancelled,
    TimedOut,
    BackendUnavailable,
};

struct Result {
    Operation operation = Operation::Status;
    ResultKind kind = ResultKind::BackendUnavailable;
    int code = -1;
    int exit_code = 0;
    uint64_t request_id = 0;
    std::string data;
    AdbStatus status;
    bool status_valid = false;

    bool ok() const noexcept;
};

using Completion = std::function<void(Result)>;
using ProcessCallback = std::function<void(int, std::string)>;
using AdminCompleteCallback = std::function<void(int, int)>;
using AdminStartedCallback = std::function<void(int, uint64_t)>;
using CancelCompleteCallback = std::function<void(int)>;

struct Backend {
    std::function<void(std::list<std::string>, ProcessCallback)> process;
    std::function<void(std::list<std::string>, int, int, AdminCompleteCallback,
                       AdminStartedCallback)>
        system_admin;
    std::function<void(uint64_t, CancelCompleteCallback)> cancel;
};

Backend system_backend();

std::list<std::string> status_request();
std::list<std::string> set_enabled_request(bool enabled);
std::list<std::string> authorize_request(const std::string &public_key);
std::list<std::string> revoke_request(const std::string &fingerprint);
std::list<std::string> clear_authorizations_request();
std::list<std::string> reboot_request();

const char *result_kind_name(ResultKind kind) noexcept;
std::string result_message(const Result &result);

class AdbApi {
public:
    AdbApi();
    explicit AdbApi(Backend backend);
    ~AdbApi();

    AdbApi(const AdbApi &) = delete;
    AdbApi &operator=(const AdbApi &) = delete;

    bool query_status(Completion completion);
    bool set_enabled(bool enabled, Completion completion);
    bool authorize(const std::string &public_key, Completion completion);
    bool revoke(const std::string &fingerprint, Completion completion);
    bool clear_authorizations(Completion completion);
    bool pair(const std::string &public_key,
              bool enable_after_pair,
              Completion completion);
    bool reboot(Completion completion);

    bool cancel();
    void shutdown() noexcept;
    bool pending() const noexcept;

private:
    std::shared_ptr<AdbApiState> state_;
};

} // namespace settings_adb

#ifndef SETTINGS_ADB_API_NO_CP0
#include "hal_lvgl_bsp.h"
#endif

#include <atomic>
#include <cerrno>
#include <exception>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>

namespace settings_adb {

namespace {

struct ActiveRequest {
    Operation operation = Operation::Status;
    Completion completion;
    uint64_t generation = 0;
    std::atomic_bool cancel_requested{false};
    std::atomic_bool started_seen{false};
    std::atomic<int> start_code{0};
    std::atomic<uint64_t> request_id{0};
};

} // namespace

struct AdbApiState {
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

bool is_current(const std::shared_ptr<AdbApiState> &state,
                const std::shared_ptr<ActiveRequest> &request)
{
    if (!state || !request) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->accepting && state->active == request &&
           request->generation == state->generation;
}

std::shared_ptr<ActiveRequest> begin_request(
    const std::shared_ptr<AdbApiState> &state,
    Operation operation,
    Completion completion)
{
    if (!state) return {};
    try {
        auto request = std::make_shared<ActiveRequest>();
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

void finish_request(const std::shared_ptr<AdbApiState> &state,
                    const std::shared_ptr<ActiveRequest> &request,
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

void finish_start_failure(const std::shared_ptr<AdbApiState> &state,
                          const std::shared_ptr<ActiveRequest> &request,
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

void handle_process_completion(const std::shared_ptr<AdbApiState> &state,
                               const std::shared_ptr<ActiveRequest> &request,
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

void handle_admin_started(const std::shared_ptr<AdbApiState> &state,
                          const std::shared_ptr<ActiveRequest> &request,
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

void handle_admin_completion(const std::shared_ptr<AdbApiState> &state,
                             const std::shared_ptr<ActiveRequest> &request,
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

bool Result::ok() const noexcept
{
    return kind == ResultKind::Success;
}

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

AdbApi::AdbApi(Backend backend) : state_(std::make_shared<AdbApiState>())
{
    state_->backend = std::move(backend);
}

AdbApi::~AdbApi()
{
    shutdown();
}

void deliver_busy(Operation operation, Completion completion)
{
    invoke_completion(std::move(completion),
                      make_result(operation, ResultKind::Busy, -EBUSY));
}

void deliver_invalid(Operation operation, Completion completion, int code = -22)
{
    invoke_completion(std::move(completion),
                      make_result(operation, ResultKind::InvalidRequest, code));
}

bool start_process(const std::shared_ptr<AdbApiState> &state,
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

    const std::weak_ptr<AdbApiState> weak_state = state;
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

bool start_admin(const std::shared_ptr<AdbApiState> &state,
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

    const std::weak_ptr<AdbApiState> weak_state = state;
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

bool start_pair_enable(const std::shared_ptr<AdbApiState> &state,
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

    const std::shared_ptr<AdbApiState> state = state_;
    Completion final_completion = std::move(completion);
    Completion authorize_completion =
        [state, enable_after_pair, final_completion = std::move(final_completion)](
            Result result) mutable {
            result.operation = Operation::Pair;
            if (!result.ok() || !enable_after_pair) {
                invoke_completion(std::move(final_completion), std::move(result));
                return;
            }
            const bool started = start_pair_enable(
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
    const std::shared_ptr<AdbApiState> state = state_;
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
    const std::shared_ptr<AdbApiState> state = state_;
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
    const std::shared_ptr<AdbApiState> state = state_;
    if (!state) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->accepting && static_cast<bool>(state->active);
}

} // namespace settings_adb


#include <functional>
#include <string>
#include <utility>


namespace {

std::mutex adb_toggle_mutex;
bool adb_toggle_enabled = false;
bool adb_toggle_initialized = false;
bool adb_toggle_pending = false;

settings_adb::AdbApi &adb_toggle_api()
{
    static auto *api = new settings_adb::AdbApi();
    return *api;
}

bool start_adb_toggle_status_refresh()
{
    {
        std::lock_guard<std::mutex> lock(adb_toggle_mutex);
        if (adb_toggle_pending || adb_toggle_initialized) return false;
        adb_toggle_pending = true;
    }

    try {
        std::thread([] {
            bool started = false;
            try {
                started = adb_toggle_api().query_status([](settings_adb::Result result) {
                    std::lock_guard<std::mutex> lock(adb_toggle_mutex);
                    adb_toggle_pending = false;
                    if (result.ok() && result.status_valid) {
                        adb_toggle_enabled = result.status.enabled;
                        adb_toggle_initialized = true;
                    }
                });
            } catch (...) {
                started = false;
            }
            if (!started) {
                std::lock_guard<std::mutex> lock(adb_toggle_mutex);
                adb_toggle_pending = false;
            }
        }).detach();
    } catch (...) {
        std::lock_guard<std::mutex> lock(adb_toggle_mutex);
        adb_toggle_pending = false;
    }
    return true;
}

} // namespace

void LvSettingAdbGuidePage3::toggle_setting(int cmd, void *data)
{
    if (cmd == SettingApiReadFlag && data) {
        bool should_refresh = false;
        {
            std::lock_guard<std::mutex> lock(adb_toggle_mutex);
            should_refresh = !adb_toggle_pending && !adb_toggle_initialized;
        }
        if (should_refresh) start_adb_toggle_status_refresh();
        std::lock_guard<std::mutex> lock(adb_toggle_mutex);
        *static_cast<bool *>(data) = adb_toggle_enabled;
        return;
    }
    if (cmd == SettingApiReadFlagTimeStart && data) {
        auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
        bool pending = false;
        {
            std::lock_guard<std::mutex> lock(adb_toggle_mutex);
            std::get<0>(*result) = adb_toggle_enabled;
            pending = adb_toggle_pending;
        }
        if (!pending) pending = start_adb_toggle_status_refresh();
        if (std::get<1>(*result)) std::get<1>(*result)->store(pending);
        return;
    }
    if (cmd != SettingApiActivate) return;

    bool desired = false;
    {
        std::lock_guard<std::mutex> lock(adb_toggle_mutex);
        if (adb_toggle_pending) return;
        desired = !adb_toggle_enabled;
        adb_toggle_pending = true;
    }
    bool started = false;
    try {
        started = adb_toggle_api().set_enabled(desired, [desired](settings_adb::Result result) {
            std::lock_guard<std::mutex> lock(adb_toggle_mutex);
            adb_toggle_pending = false;
            if (result.ok()) {
                adb_toggle_enabled = desired;
                adb_toggle_initialized = true;
            }
        });
    } catch (...) {
        started = false;
    }
    if (!started) {
        std::lock_guard<std::mutex> lock(adb_toggle_mutex);
        adb_toggle_pending = false;
    }
}

LvSettingAdbGuidePage3::LvSettingAdbGuidePage3()
    : adb_api_(std::make_unique<settings_adb::AdbApi>())
{}

LvSettingAdbGuidePage3::LvSettingAdbGuidePage3(lv_obj_t *parent,
                                               const NodeIter &page_node,
                                               std::function<void()> back_callback,
                                               bool enabling)
    : page_node_(page_node), adb_api_(std::make_unique<settings_adb::AdbApi>()), enabling_(enabling)
{
    LeaveSelfPage = std::move(back_callback);
    create_ui(parent);
}

void LvSettingAdbGuidePage3::AnimateNextIn(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingAdbGuidePage3::AnimateNextOut(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingAdbGuidePage3::LoadNextPage() {}

void LvSettingAdbGuidePage3::LeaveNextPage()
{
    leave_page();
}

LvSettingAdbGuidePage3::~LvSettingAdbGuidePage3()
{
    leaving_ = true;
    dispatch_.cancel();
    adb_api_->shutdown();
    if (api_timer_) {
        lv_timer_delete(api_timer_);
        api_timer_ = nullptr;
    }
    stop_animation();
    if (ComponensObj) {
        lv_obj_delete(ComponensObj);
        ComponensObj = nullptr;
    }
}

void LvSettingAdbGuidePage3::create_ui(lv_obj_t *parent)
{
    if (!parent) return;

    ComponensObj = lv_obj_create(parent);
    if (!ComponensObj) return;
    lv_obj_set_size(ComponensObj, metric(LayoutMetric::ScreenW), metric(LayoutMetric::ScreenH));
    lv_obj_set_pos(ComponensObj, 0, 0);
    lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
    DComponens::lvgl_bind_event(
        ComponensObj,
        LV_EVENT_KEY,
        nullptr,
        std::bind(&LvSettingAdbGuidePage3::handle_key_event, this, std::placeholders::_1));

    const lv_font_t *title_font =
        cp0_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD);
    const lv_font_t *text_font = &lv_font_montserrat_10;
    title_label_ = add_label(8, 2, "Enable ADB - switch USB to device", 0xECECEC,
                             title_font ? title_font : &lv_font_montserrat_12);
    add_chip(86, 24, 146, 50, 0x282A30, 0x5A5C64, 6, 2);
    add_label(120, 28, "CardputerZero", 0x9A9AA0, text_font);
    add_chip(218, 30, 12, 12, 0x101012, 0x5A5C64, 3, 2);
    add_chip(228, 32, 22, 8, 0xCDCDD2, 0xCDCDD2, 2, 0);
    add_chip(250, 34, 60, 4, 0x6A6C72, 0x6A6C72, 2, 0);
    add_label(232, 42, "USB-C", 0x46DC87, text_font);
    add_chip(24, 28, 32, 44, 0x1A1A1C, 0x5A5C64, 6, 2);
    add_chip(33, 33, 14, 34, 0x0E0E10, 0x0E0E10, 4, 0);
    usb_label_ = add_label(26, 14, "USB", 0x46DC87, text_font);
    hub_label_ = add_label(28, 72, "HUB", 0xEB5F5F, text_font);

    knob_ = add_chip(32, 54, 16, 10, 0x46DC87, 0x2A6F49, 3, 1);
    step_one_label_ = add_label(8, 80, "1  Slide LEFT switch  HUB -> USB", 0xECECEC, text_font);
    step_two_label_ = add_label(8, 95, "2  USB hub & peripherals turn OFF", 0xF0C850, text_font);
    step_three_label_ = add_label(8, 110, "3  Cable -> top-right USB-C port", 0x46DC87, text_font);
    confirm_label_ = add_label(8, metric(LayoutMetric::ScreenH) - 16,
                                "OK: reboot now     ESC: later", 0x9A9AA0, text_font);
    api_timer_ = lv_timer_create(api_timer_cb, 30, this);
    render_guide();
    if (api_timer_) {
        start_animation();
        request_status();
    } else if (confirm_label_) {
        lv_label_set_text(confirm_label_, "ADB status unavailable     ESC: back");
    }
}

lv_obj_t *LvSettingAdbGuidePage3::create_chip(lv_obj_t *parent, int pos_x, int pos_y, int width,
                                              int height, uint32_t background, uint32_t border,
                                              int radius, int border_width)
{
    if (!parent) return nullptr;
    lv_obj_t *chip = lv_obj_create(parent);
    if (!chip) return nullptr;
    lv_obj_set_pos(chip, pos_x, pos_y);
    lv_obj_set_size(chip, width, height);
    lv_obj_set_style_radius(chip, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip, lv_color_hex(background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(chip, lv_color_hex(border), LV_PART_MAIN);
    lv_obj_set_style_border_width(chip, border_width, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chip, 0, LV_PART_MAIN);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    return chip;
}

lv_obj_t *LvSettingAdbGuidePage3::create_label(lv_obj_t *parent, int pos_x, int pos_y,
                                               const char *text, uint32_t color,
                                               const lv_font_t *font)
{
    if (!parent) return nullptr;
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, pos_x, pos_y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    return label;
}

lv_obj_t *LvSettingAdbGuidePage3::add_chip(int pos_x, int pos_y, int width, int height,
                                           uint32_t background, uint32_t border, int radius,
                                           int border_width)
{
    return create_chip(ComponensObj, pos_x, pos_y, width, height, background, border, radius,
                       border_width);
}

lv_obj_t *LvSettingAdbGuidePage3::add_label(int pos_x, int pos_y, const char *text, uint32_t color,
                                            const lv_font_t *font)
{
    return create_label(ComponensObj, pos_x, pos_y, text, color, font);
}

void LvSettingAdbGuidePage3::stop_animation()
{
    if (!knob_) return;
    lv_anim_del(knob_, nullptr);
}

void LvSettingAdbGuidePage3::start_animation()
{
    if (!knob_ || reboot_pending_ || reboot_requested_) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, knob_);
    lv_anim_set_values(&animation, enabling_ ? 54 : 34, enabling_ ? 34 : 54);
    lv_anim_set_time(&animation, 650);
    lv_anim_set_playback_time(&animation, 650);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(
        &animation,
        [](void *object, int32_t value) {
            if (object) lv_obj_set_y(static_cast<lv_obj_t *>(object), value);
        });
    lv_anim_start(&animation);
}

void LvSettingAdbGuidePage3::render_guide()
{
    if (title_label_)
        lv_label_set_text(title_label_, enabling_ ? "Enable ADB - switch USB to device"
                                                   : "Disable ADB - switch USB to hub");
    if (usb_label_)
        lv_obj_set_style_text_color(usb_label_, lv_color_hex(enabling_ ? 0x46DC87 : 0x9A9AA0),
                                    LV_PART_MAIN);
    if (hub_label_)
        lv_obj_set_style_text_color(hub_label_, lv_color_hex(enabling_ ? 0xEB5F5F : 0x46DC87),
                                    LV_PART_MAIN);
    if (step_one_label_)
        lv_label_set_text(step_one_label_, enabling_ ? "1  Slide LEFT switch  HUB -> USB"
                                                     : "1  Slide LEFT switch  USB -> HUB");
    if (step_two_label_)
        lv_label_set_text(step_two_label_, enabling_ ? "2  USB hub & peripherals turn OFF"
                                                     : "2  USB hub & peripherals come back");
    if (step_three_label_)
        lv_label_set_text(step_three_label_, enabling_ ? "3  Cable -> top-right USB-C port"
                                                       : "3  Reboot to apply the change");
    if (confirm_label_ && !reboot_pending_ && !reboot_requested_)
        lv_label_set_text(confirm_label_, "OK: reboot now     ESC: later");
}

void LvSettingAdbGuidePage3::api_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = timer ? static_cast<LvSettingAdbGuidePage3 *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!self || timer != self->api_timer_ || self->leaving_) return;
    self->dispatch_.drain();
}

void LvSettingAdbGuidePage3::request_status()
{
    if (leaving_ || !api_timer_) return;
    status_pending_ = true;
    const auto token = dispatch_.token();
    if (adb_api_->query_status([token, this](settings_adb::Result result) mutable {
            SettingsAsync::Dispatch::enqueue_from_callback(
                token,
                [this, result = std::move(result)]() mutable {
                    if (!ComponensObj || leaving_) return;
                    status_pending_ = false;
                    if (!result.ok() || !result.status_valid) {
                        if (confirm_label_) {
                            const std::string message = settings_adb::result_message(result);
                            lv_label_set_text(confirm_label_, message.c_str());
                        }
                        return;
                    }
                    enabling_ = !result.status.enabled;
                    render_guide();
                    stop_animation();
                    start_animation();
                });
        }))
        return;
    status_pending_ = false;
}

void LvSettingAdbGuidePage3::request_reboot()
{
    if (leaving_ || !api_timer_ || reboot_pending_ || reboot_requested_) return;
    dispatch_.advance_generation();
    if (status_pending_) {
        adb_api_->cancel();
        status_pending_ = false;
    }
    stop_animation();
    reboot_pending_ = true;
    if (confirm_label_) lv_label_set_text(confirm_label_, "Rebooting...");

    const auto token = dispatch_.token();
    adb_api_->reboot([token, this](settings_adb::Result result) mutable {
        SettingsAsync::Dispatch::enqueue_from_callback(
            token,
            [this, result = std::move(result)]() mutable {
                if (!ComponensObj || leaving_) return;
                reboot_pending_ = false;
                if (result.ok()) {
                    reboot_requested_ = true;
                    if (confirm_label_)
                        lv_label_set_text(confirm_label_, "Reboot requested     ESC: back");
                    return;
                }
                if (confirm_label_)
                    lv_label_set_text(confirm_label_, settings_adb::result_message(result).c_str());
                start_animation();
            });
    });
}

void LvSettingAdbGuidePage3::leave_page()
{
    if (leaving_) return;
    leaving_ = true;
    dispatch_.advance_generation();
    adb_api_->cancel();
    stop_animation();
    if (api_timer_) {
        lv_timer_delete(api_timer_);
        api_timer_ = nullptr;
    }
    if (LeaveSelfPage) LeaveSelfPage();
}

void LvSettingAdbGuidePage3::handle_key_event(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        lv_event_stop_processing(event);
        leave_page();
        return;
    } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
        request_reboot();
    }
    lv_event_stop_processing(event);
}
