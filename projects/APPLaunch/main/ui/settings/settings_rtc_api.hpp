#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <list>
#include <string>
#include <string_view>
#include <vector>

namespace setting {

enum class RtcField { YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, COUNT };

enum class NtpToggleEligibility { ALLOWED, IN_FLIGHT, DIRTY, UNAVAILABLE };

enum class RtcConfirmInput { SELECT_SAVE, SELECT_DISCARD, CONFIRM, CANCEL };
enum class RtcConfirmAction { NONE, SAVE, DISCARD };

class RtcWriteConfirmModel {
public:
    void reset() noexcept { save_selected_ = false; }
    bool save_selected() const noexcept { return save_selected_; }
    RtcConfirmAction handle(RtcConfirmInput input) noexcept;

private:
    bool save_selected_ = false;
};

class RtcOverlayLifecycleModel {
public:
    using Token = std::uint64_t;

    Token open() noexcept;
    bool close(Token token) noexcept;
    bool active() const noexcept { return active_; }

private:
    bool active_ = false;
    Token generation_ = 0;
};

class RtcStateModel {
public:
    using Values = std::array<int, static_cast<unsigned>(RtcField::COUNT)>;

    const Values &values() const noexcept { return values_; }
    bool dirty() const noexcept { return dirty_; }
    bool ntp_on() const noexcept { return ntp_on_; }
    bool ntp_available() const noexcept { return ntp_available_; }

    void set_ntp_status(int status) noexcept;
    void rollback_ntp(bool previous) noexcept { ntp_on_ = previous; }
    NtpToggleEligibility ntp_toggle_eligibility(bool in_flight) const noexcept;

    bool load_local_time(std::string_view payload);
    bool load_local_time(const std::tm &value) noexcept;

    int field_min(RtcField field) const noexcept;
    int field_max(RtcField field) const noexcept;
    int field_value(RtcField field) const noexcept;
    bool edit_field(RtcField field, int value) noexcept;
    std::vector<std::string> field_options(RtcField field) const;
    int field_selection_index(RtcField field) const noexcept;
    bool edit_field_selection(RtcField field, std::size_t selection) noexcept;

    void discard_edits() noexcept { dirty_ = false; }
    void finish_commit(bool succeeded) noexcept { dirty_ = !succeeded; }
    std::string timestamp() const;
    std::list<std::string> commit_request() const;

    static int days_in_month(int year, int month) noexcept;
    static std::string_view field_name(RtcField field) noexcept;
    static bool field_from_index(int index, RtcField &field) noexcept;
    static bool field_from_name(std::string_view name, RtcField &field) noexcept;
    static bool parse_local_time_payload(std::string_view payload, Values &values) noexcept;
    static bool parse_timestamp(std::string_view timestamp, Values &values) noexcept;
    static bool is_valid(const Values &values) noexcept;

private:
    static unsigned field_index(RtcField field) noexcept;
    static bool parse_decimal(std::string_view value, int &result) noexcept;

    Values values_ = {2026, 1, 1, 0, 0, 0};
    bool ntp_on_ = true;
    bool ntp_available_ = true;
    bool dirty_ = false;
};

} // namespace setting

namespace settings_rtc {

using RtcField = setting::RtcField;
using RtcStateModel = setting::RtcStateModel;
using RtcWriteConfirmModel = setting::RtcWriteConfirmModel;
using RtcOverlayLifecycleModel = setting::RtcOverlayLifecycleModel;
using RtcConfirmInput = setting::RtcConfirmInput;
using RtcConfirmAction = setting::RtcConfirmAction;
using NtpToggleEligibility = setting::NtpToggleEligibility;
using RtcValues = RtcStateModel::Values;

enum class PrivilegedResultKind { SUCCESS, AUTH_FAILED, CANCELLED, TIMED_OUT, EXEC_FAILED };

constexpr int kApiErrorInvalidArgument = -22;
constexpr int kApiErrorInvocation = -5;
constexpr int kApiErrorInvalidPayload = -74;

struct NtpReadResult {
    int status = -1;
    bool available = false;
    bool enabled = false;
    std::string payload;
};

struct TimeReadResult {
    int code = -1;
    bool valid = false;
    RtcValues values{};
    std::string payload;
};

struct RefreshResult {
    NtpReadResult ntp;
    TimeReadResult time;

    bool valid() const noexcept
    {
        return ntp.available && time.valid;
    }
};

struct PrivilegedResult {
    int result_code = 1;
    int exit_code = 0;
    PrivilegedResultKind kind = PrivilegedResultKind::EXEC_FAILED;

    bool succeeded() const noexcept
    {
        return kind == PrivilegedResultKind::SUCCESS && result_code == 0 && exit_code == 0;
    }
};

PrivilegedResultKind classify_privileged_result(int result) noexcept;
PrivilegedResultKind classify_privileged_result(int result, int exit_code) noexcept;

using NtpReadCallback = std::function<void(NtpReadResult)>;
using TimeReadCallback = std::function<void(TimeReadResult)>;
using RefreshCallback = std::function<void(RefreshResult)>;
using PrivilegedCallback = std::function<void(PrivilegedResult)>;
using RequestStartedCallback = std::function<void(int, std::uint64_t)>;

int read_ntp_async(NtpReadCallback callback);
int read_local_time_async(TimeReadCallback callback);
int refresh_async(RefreshCallback callback);
int set_ntp_async(bool enabled,
                  PrivilegedCallback callback,
                  RequestStartedCallback started = {});
int set_time_async(std::string timestamp,
                   PrivilegedCallback callback,
                   RequestStartedCallback started = {});
int cancel_request(std::uint64_t request_id);
void settings_rtc_ntp_api(int command, void *data) noexcept;
void update_ntp_cache(int status) noexcept;

enum class WorkflowOperation { IDLE, NTP_SET, TIME_SET };

enum class CommitEligibility { ALLOWED, NO_EDITS, NTP_ENABLED, IN_FLIGHT };

class RtcWorkflowModel {
public:
    RtcStateModel &state() noexcept { return state_; }
    const RtcStateModel &state() const noexcept { return state_; }

    WorkflowOperation operation() const noexcept { return operation_; }
    bool pending() const noexcept { return operation_ != WorkflowOperation::IDLE; }
    bool ntp_pending() const noexcept { return operation_ == WorkflowOperation::NTP_SET; }
    bool time_pending() const noexcept { return operation_ == WorkflowOperation::TIME_SET; }

    void reset() noexcept;
    void set_ntp_status(int status) noexcept { state_.set_ntp_status(status); }
    bool load_local_time(std::string_view payload) { return state_.load_local_time(payload); }
    bool load_local_time(const std::tm &value) noexcept { return state_.load_local_time(value); }
    bool edit_field(RtcField field, int value) noexcept { return state_.edit_field(field, value); }
    bool edit_field_selection(RtcField field, std::size_t selection) noexcept
    {
        return state_.edit_field_selection(field, selection);
    }
    void discard_edits() noexcept { state_.discard_edits(); }

    CommitEligibility commit_eligibility() const noexcept;
    bool begin_time_commit() noexcept;
    void finish_time_commit(bool succeeded) noexcept;
    void cancel_time_commit() noexcept;

    bool begin_ntp_toggle(bool desired) noexcept;
    void finish_ntp_toggle(bool succeeded, int actual_status = -1) noexcept;
    void cancel_ntp_toggle() noexcept;

private:
    RtcStateModel state_;
    WorkflowOperation operation_ = WorkflowOperation::IDLE;
    bool ntp_previous_ = true;
    bool ntp_desired_ = true;
};

RtcWorkflowModel &session() noexcept;

} // namespace settings_rtc
