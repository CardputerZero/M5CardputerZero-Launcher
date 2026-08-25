#pragma once

#include "settings_adb_state.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

namespace settings_adb {

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

    bool ok() const noexcept { return kind == ResultKind::Success; }
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
    struct State;
    struct ActiveRequest;

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
    std::shared_ptr<State> state_;

    static bool start_process(const std::shared_ptr<State> &state,
                              Operation operation,
                              Completion completion,
                              std::list<std::string> arguments);
    static bool start_admin(const std::shared_ptr<State> &state,
                            Operation operation,
                            Completion completion,
                            std::list<std::string> arguments,
                            int auth_timeout_ms,
                            int exec_timeout_ms);
    static bool start_pair_enable(const std::shared_ptr<State> &state,
                                  Completion completion);
    static void deliver_busy(Operation operation, Completion completion);
    static void deliver_invalid(Operation operation,
                                Completion completion,
                                int code = -22);
};

} // namespace settings_adb
