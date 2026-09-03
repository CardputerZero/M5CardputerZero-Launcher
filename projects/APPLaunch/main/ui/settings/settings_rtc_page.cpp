#include "settings_rtc_page.hpp"
#include "settings_fonts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <list>
#include <string>
#include <string_view>
#include <vector>
#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <system_error>
#include <tuple>
#include <utility>
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

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

enum class ApiError : int {
    InvalidArgument = -22,
    Invocation = -5,
    InvalidPayload = -74,
};

constexpr int api_error_code(ApiError error) noexcept
{
    return static_cast<int>(error);
}

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



namespace setting {
namespace {

bool all_digits(std::string_view value)
{
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}
bool parse_fixed_decimal(std::string_view value, int &result)
{
    if (!all_digits(value)) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

bool split_csv(std::string_view payload, RtcStateModel::Values &values)
{
    std::size_t start = 0;
    for (unsigned index = 0; index < static_cast<unsigned>(RtcField::COUNT); ++index) {
        const std::size_t separator = payload.find(',', start);
        const std::size_t end = separator == std::string_view::npos ? payload.size() : separator;
        if (!parse_fixed_decimal(payload.substr(start, end - start), values[index])) return false;
        if (index + 1 == static_cast<unsigned>(RtcField::COUNT))
            return separator == std::string_view::npos;
        if (separator == std::string_view::npos) return false;
        start = separator + 1;
    }
    return false;
}

bool parse_timestamp_value(std::string_view timestamp, RtcStateModel::Values &values)
{
    if (timestamp.size() != 19 || timestamp[4] != '-' || timestamp[7] != '-' ||
        timestamp[10] != ' ' || timestamp[13] != ':' || timestamp[16] != ':')
        return false;

    return parse_fixed_decimal(timestamp.substr(0, 4), values[0]) &&
        parse_fixed_decimal(timestamp.substr(5, 2), values[1]) &&
        parse_fixed_decimal(timestamp.substr(8, 2), values[2]) &&
        parse_fixed_decimal(timestamp.substr(11, 2), values[3]) &&
        parse_fixed_decimal(timestamp.substr(14, 2), values[4]) &&
        parse_fixed_decimal(timestamp.substr(17, 2), values[5]);
}

} // namespace

RtcConfirmAction RtcWriteConfirmModel::handle(RtcConfirmInput input) noexcept
{
    switch (input) {
    case RtcConfirmInput::SELECT_SAVE:
        save_selected_ = true;
        return RtcConfirmAction::NONE;
    case RtcConfirmInput::SELECT_DISCARD:
        save_selected_ = false;
        return RtcConfirmAction::NONE;
    case RtcConfirmInput::CONFIRM:
        return save_selected_ ? RtcConfirmAction::SAVE : RtcConfirmAction::DISCARD;
    case RtcConfirmInput::CANCEL:
        return RtcConfirmAction::DISCARD;
    }
    return RtcConfirmAction::NONE;
}

RtcOverlayLifecycleModel::Token RtcOverlayLifecycleModel::open() noexcept
{
    if (active_) return 0;
    active_ = true;
    if (++generation_ == 0) ++generation_;
    return generation_;
}

bool RtcOverlayLifecycleModel::close(Token token) noexcept
{
    if (!active_ || token == 0 || token != generation_) return false;
    active_ = false;
    return true;
}

void RtcStateModel::set_ntp_status(int status) noexcept
{
    ntp_available_ = status == 0 || status == 1;
    if (ntp_available_) ntp_on_ = status == 1;
}

NtpToggleEligibility RtcStateModel::ntp_toggle_eligibility(bool in_flight) const noexcept
{
    if (in_flight) return NtpToggleEligibility::IN_FLIGHT;
    if (dirty_) return NtpToggleEligibility::DIRTY;
    if (!ntp_available_) return NtpToggleEligibility::UNAVAILABLE;
    return NtpToggleEligibility::ALLOWED;
}

bool RtcStateModel::load_local_time(std::string_view payload)
{
    Values parsed{};
    if (!parse_local_time_payload(payload, parsed) || !is_valid(parsed)) return false;
    values_ = parsed;
    dirty_ = false;
    return true;
}

bool RtcStateModel::load_local_time(const std::tm &value) noexcept
{
    Values parsed = {
        value.tm_year + 1900,
        value.tm_mon + 1,
        value.tm_mday,
        value.tm_hour,
        value.tm_min,
        value.tm_sec,
    };
    if (!is_valid(parsed)) return false;
    values_ = parsed;
    dirty_ = false;
    return true;
}

int RtcStateModel::field_min(RtcField field) const noexcept
{
    static constexpr int minimums[] = {2000, 1, 1, 0, 0, 0};
    const unsigned index = field_index(field);
    return index < static_cast<unsigned>(RtcField::COUNT) ? minimums[index] : 0;
}

int RtcStateModel::field_max(RtcField field) const noexcept
{
    static constexpr int maximums[] = {2099, 12, 31, 23, 59, 59};
    const unsigned index = field_index(field);
    if (field == RtcField::DAY)
        return days_in_month(values_[field_index(RtcField::YEAR)], values_[field_index(RtcField::MONTH)]);
    return index < static_cast<unsigned>(RtcField::COUNT) ? maximums[index] : 0;
}

int RtcStateModel::field_value(RtcField field) const noexcept
{
    const unsigned index = field_index(field);
    return index < values_.size() ? values_[index] : 0;
}

bool RtcStateModel::edit_field(RtcField field, int value) noexcept
{
    const unsigned index = field_index(field);
    if (index >= values_.size() || value < field_min(field) || value > field_max(field)) return false;

    values_[index] = value;
    if (field == RtcField::YEAR || field == RtcField::MONTH) {
        const int maximum_day = field_max(RtcField::DAY);
        if (values_[field_index(RtcField::DAY)] > maximum_day)
            values_[field_index(RtcField::DAY)] = maximum_day;
    }
    dirty_ = true;
    return true;
}

std::vector<std::string> RtcStateModel::field_options(RtcField field) const
{
    std::vector<std::string> options;
    const unsigned index = field_index(field);
    if (index >= static_cast<unsigned>(RtcField::COUNT)) return options;

    const int minimum = field_min(field);
    const int maximum = field_max(field);
    if (maximum < minimum) return options;

    options.reserve(static_cast<std::size_t>(maximum - minimum + 1));
    for (int value = minimum; value <= maximum; ++value)
        options.push_back(std::to_string(value));
    return options;
}

int RtcStateModel::field_selection_index(RtcField field) const noexcept
{
    const unsigned index = field_index(field);
    if (index >= values_.size()) return -1;
    const int selection = field_value(field) - field_min(field);
    return selection >= 0 && selection <= field_max(field) - field_min(field) ? selection : -1;
}

bool RtcStateModel::edit_field_selection(RtcField field, std::size_t selection) noexcept
{
    const int minimum = field_min(field);
    const int maximum = field_max(field);
    if (maximum < minimum || selection > static_cast<std::size_t>(maximum - minimum)) return false;
    return edit_field(field, minimum + static_cast<int>(selection));
}

std::string RtcStateModel::timestamp() const
{
    char buffer[32] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04d-%02d-%02d %02d:%02d:%02d",
                  values_[0],
                  values_[1],
                  values_[2],
                  values_[3],
                  values_[4],
                  values_[5]);
    return buffer;
}

std::list<std::string> RtcStateModel::commit_request() const
{
    return {"TimeSet", timestamp()};
}

int RtcStateModel::days_in_month(int year, int month) noexcept
{
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month != 2) return days[month - 1];
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return leap ? 29 : 28;
}

std::string_view RtcStateModel::field_name(RtcField field) noexcept
{
    static constexpr std::string_view names[] = {
        "Year", "Month", "Day", "Hour", "Minute", "Second",
    };
    const unsigned index = field_index(field);
    return index < static_cast<unsigned>(RtcField::COUNT) ? names[index] : std::string_view{};
}

bool RtcStateModel::field_from_index(int index, RtcField &field) noexcept
{
    if (index < 0 || index >= static_cast<int>(RtcField::COUNT)) return false;
    field = static_cast<RtcField>(index);
    return true;
}

bool RtcStateModel::field_from_name(std::string_view name, RtcField &field) noexcept
{
    for (unsigned index = 0; index < static_cast<unsigned>(RtcField::COUNT); ++index) {
        const auto candidate = field_name(static_cast<RtcField>(index));
        if (candidate == name) {
            field = static_cast<RtcField>(index);
            return true;
        }
    }
    return false;
}

bool RtcStateModel::parse_local_time_payload(std::string_view payload, Values &values) noexcept
{
    return split_csv(payload, values) || parse_timestamp_value(payload, values);
}

bool RtcStateModel::parse_timestamp(std::string_view timestamp, Values &values) noexcept
{
    return parse_timestamp_value(timestamp, values) && is_valid(values);
}

bool RtcStateModel::is_valid(const Values &values) noexcept
{
    if (values[0] < 2000 || values[0] > 2099 || values[1] < 1 || values[1] > 12) return false;
    return values[2] >= 1 && values[2] <= days_in_month(values[0], values[1]) &&
        values[3] >= 0 && values[3] <= 23 && values[4] >= 0 && values[4] <= 59 &&
        values[5] >= 0 && values[5] <= 59;
}

unsigned RtcStateModel::field_index(RtcField field) noexcept
{
    return static_cast<unsigned>(field);
}

bool RtcStateModel::parse_decimal(std::string_view value, int &result) noexcept
{
    return parse_fixed_decimal(value, result);
}

} // namespace setting

namespace settings_rtc {

PrivilegedResultKind classify_privileged_result(int result) noexcept
{
    switch (result) {
    case 0: return PrivilegedResultKind::SUCCESS;
    case 1: return PrivilegedResultKind::AUTH_FAILED;
    case 3: return PrivilegedResultKind::CANCELLED;
    case 4: return PrivilegedResultKind::TIMED_OUT;
    default: return PrivilegedResultKind::EXEC_FAILED;
    }
}

PrivilegedResultKind classify_privileged_result(int result, int exit_code) noexcept
{
    if (result == 0 && exit_code != 0) return PrivilegedResultKind::EXEC_FAILED;
    return classify_privileged_result(result);
}

namespace {

struct NtpAdapterState {
    std::mutex mutex;
    bool available = true;
    bool enabled = true;
    bool pending = false;
    bool initialized = false;
};

NtpAdapterState &ntp_adapter_state()
{
    static NtpAdapterState state;
    return state;
}

void refresh_ntp_cache()
{
    auto &state = ntp_adapter_state();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.pending || state.initialized) return;
        state.pending = true;
    }

    const int result = read_ntp_async([](NtpReadResult value) {
        session().set_ntp_status(value.status);
        auto &state = ntp_adapter_state();
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.pending = false;
            state.initialized = true;
            state.available = value.available;
            if (value.available) state.enabled = value.enabled;
        }
    });
    if (result != 0) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pending = false;
        state.initialized = true;
        state.available = false;
    }
}

template <typename Callback, typename Value>
void invoke_noexcept(const Callback &callback, Value value) noexcept
{
    if (!callback) return;
    try {
        callback(std::move(value));
    } catch (...) {
    }
}

bool parse_time_of_day(std::string_view value, int &hour, int &minute) noexcept
{
    if (value.size() != 5 || value[2] != ':' ||
        value[0] < '0' || value[0] > '9' || value[1] < '0' || value[1] > '9' ||
        value[3] < '0' || value[3] > '9' || value[4] < '0' || value[4] > '9')
        return false;
    hour = (value[0] - '0') * 10 + value[1] - '0';
    minute = (value[3] - '0') * 10 + value[4] - '0';
    return hour <= 23 && minute <= 59;
}

NtpReadResult make_ntp_result(int status, std::string payload)
{
    NtpReadResult result;
    result.status = status;
    result.available = status == 0 || status == 1;
    result.enabled = status == 1;
    result.payload = std::move(payload);
    return result;
}

TimeReadResult make_time_result(int code, std::string payload)
{
    TimeReadResult result;
    result.code = code;
    result.payload = std::move(payload);
    if (code == 0)
        result.valid = RtcStateModel::parse_local_time_payload(result.payload, result.values) &&
            RtcStateModel::is_valid(result.values);
    if (!result.valid && result.code == 0) result.code = api_error_code(ApiError::InvalidPayload);
    return result;
}

std::string format_timestamp(const RtcValues &values)
{
    char buffer[32] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04d-%02d-%02d %02d:%02d:%02d",
                  values[0],
                  values[1],
                  values[2],
                  values[3],
                  values[4],
                  values[5]);
    return buffer;
}

bool current_local_time(RtcValues &values) noexcept
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &now) != 0) return false;
#else
    if (localtime_r(&now, &local) == nullptr) return false;
#endif
    values = {local.tm_year + 1900,
              local.tm_mon + 1,
              local.tm_mday,
              local.tm_hour,
              local.tm_min,
              local.tm_sec};
    return RtcStateModel::is_valid(values);
}

void read_time_fallback(const TimeReadCallback &callback) noexcept
{
    char buffer[64] = {};
    try {
        cp0_time_str(buffer, static_cast<int>(sizeof(buffer)));
    } catch (...) {
        invoke_noexcept(callback, make_time_result(api_error_code(ApiError::Invocation), {}));
        return;
    }

    TimeReadResult result = make_time_result(0, buffer);
    if (!result.valid) {
        int hour = 0;
        int minute = 0;
        RtcValues values{};
        if (parse_time_of_day(buffer, hour, minute) && current_local_time(values)) {
            values[3] = hour;
            values[4] = minute;
            result.values = values;
            result.valid = RtcStateModel::is_valid(values);
            if (result.valid) result.payload = format_timestamp(values);
        }
    }
    if (!result.valid) result.code = api_error_code(ApiError::InvalidPayload);
    invoke_noexcept(callback, std::move(result));
}

int submit_privileged(std::list<std::string> arguments,
                      PrivilegedCallback callback,
                      RequestStartedCallback started)
{
    if (!callback) return api_error_code(ApiError::InvalidArgument);
    try {
        cp0_signal_system_admin_async(
            std::move(arguments),
            60000,
            30000,
            [callback = std::move(callback)](int result_code, int exit_code) mutable {
                PrivilegedResult result;
                result.result_code = result_code;
                result.exit_code = exit_code;
                result.kind = classify_privileged_result(result_code, exit_code);
                invoke_noexcept(callback, std::move(result));
            },
            [started = std::move(started)](int start_code, std::uint64_t request_id) mutable {
                if (!started) return;
                try {
                    started(start_code, request_id);
                } catch (...) {
                }
            });
    } catch (...) {
        return api_error_code(ApiError::Invocation);
    }
    return 0;
}

struct RefreshState {
    std::mutex mutex;
    int remaining = 2;
    bool delivered = false;
    RefreshResult result;
    RefreshCallback callback;
};

template <typename Update>
void complete_refresh(const std::shared_ptr<RefreshState> &state, Update update) noexcept
{
    RefreshResult result;
    bool deliver = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->delivered) return;
        update(state->result);
        if (--state->remaining == 0) {
            state->delivered = true;
            result = state->result;
            deliver = true;
        }
    }
    if (deliver) invoke_noexcept(state->callback, std::move(result));
}

} // namespace

int read_ntp_async(NtpReadCallback callback)
{
    if (!callback) return api_error_code(ApiError::InvalidArgument);

    auto delivered = std::make_shared<std::atomic_bool>(false);
    auto delivery = std::make_shared<std::pair<NtpReadCallback, std::shared_ptr<std::atomic_bool>>>(
        std::move(callback), delivered);
    auto deliver = [delivery](NtpReadResult result) mutable {
        if (delivery->second->exchange(true, std::memory_order_acq_rel)) return;
        invoke_noexcept(delivery->first, std::move(result));
    };

    try {
        cp0_signal_osinfo_api(
            {"NtpGet"},
            [deliver = std::move(deliver)](int code, std::string payload) mutable {
                deliver(make_ntp_result(code, std::move(payload)));
            });
    } catch (...) {
        int fallback_status = -1;
        try {
            fallback_status = cp0_time_ntp_get();
        } catch (...) {
        }
        deliver(make_ntp_result(fallback_status, {}));
        return 0;
    }
    return 0;
}

int read_local_time_async(TimeReadCallback callback)
{
    if (!callback) return api_error_code(ApiError::InvalidArgument);

    auto delivered = std::make_shared<std::atomic_bool>(false);
    auto deliver = [callback = std::move(callback), delivered](TimeReadResult result) mutable {
        if (delivered->exchange(true, std::memory_order_acq_rel)) return;
        invoke_noexcept(callback, std::move(result));
    };

    auto handle_osinfo = [deliver](int code, std::string payload) mutable {
        TimeReadResult result = make_time_result(code, std::move(payload));
        if (result.valid) {
            deliver(std::move(result));
            return;
        }
        read_time_fallback(deliver);
    };

    try {
        cp0_signal_osinfo_api({"LocalTime"}, std::move(handle_osinfo));
    } catch (...) {
        read_time_fallback(deliver);
        return 0;
    }
    return 0;
}

int refresh_async(RefreshCallback callback)
{
    if (!callback) return api_error_code(ApiError::InvalidArgument);

    auto state = std::make_shared<RefreshState>();
    state->callback = std::move(callback);

    const int ntp_start = read_ntp_async([state](NtpReadResult result) {
        complete_refresh(state, [result = std::move(result)](RefreshResult &target) mutable {
            target.ntp = std::move(result);
        });
    });
    if (ntp_start != 0) {
        complete_refresh(state, [ntp_start](RefreshResult &target) {
            target.ntp = make_ntp_result(ntp_start, {});
        });
    }

    const int time_start = read_local_time_async([state](TimeReadResult result) {
        complete_refresh(state, [result = std::move(result)](RefreshResult &target) mutable {
            target.time = std::move(result);
        });
    });
    if (time_start != 0) {
        complete_refresh(state, [time_start](RefreshResult &target) {
            target.time = make_time_result(time_start, {});
        });
    }

    return ntp_start != 0 && time_start != 0 ? api_error_code(ApiError::Invocation) : 0;
}

int set_ntp_async(bool enabled, PrivilegedCallback callback, RequestStartedCallback started)
{
    return submit_privileged({"NtpSet", enabled ? "1" : "0"}, std::move(callback), std::move(started));
}

int set_time_async(std::string timestamp, PrivilegedCallback callback, RequestStartedCallback started)
{
    if (!callback) return api_error_code(ApiError::InvalidArgument);
    RtcValues parsed{};
    if (!RtcStateModel::parse_timestamp(timestamp, parsed)) return api_error_code(ApiError::InvalidArgument);
    return submit_privileged({"TimeSet", std::move(timestamp)}, std::move(callback), std::move(started));
}

int cancel_request(std::uint64_t request_id)
{
    if (request_id == 0) return api_error_code(ApiError::InvalidArgument);
    try {
        cp0_signal_sudo_cancel(request_id, [](int) {});
    } catch (...) {
        return api_error_code(ApiError::Invocation);
    }
    return 0;
}

void RtcWorkflowModel::reset() noexcept
{
    state_ = RtcStateModel{};
    operation_ = WorkflowOperation::IDLE;
    ntp_previous_ = true;
    ntp_desired_ = true;
}

CommitEligibility RtcWorkflowModel::commit_eligibility() const noexcept
{
    if (pending()) return CommitEligibility::IN_FLIGHT;
    if (state_.ntp_on()) return CommitEligibility::NTP_ENABLED;
    if (!state_.dirty()) return CommitEligibility::NO_EDITS;
    return CommitEligibility::ALLOWED;
}

bool RtcWorkflowModel::begin_time_commit() noexcept
{
    if (commit_eligibility() != CommitEligibility::ALLOWED) return false;
    operation_ = WorkflowOperation::TIME_SET;
    return true;
}

void RtcWorkflowModel::finish_time_commit(bool succeeded) noexcept
{
    if (!time_pending()) return;
    state_.finish_commit(succeeded);
    operation_ = WorkflowOperation::IDLE;
}

void RtcWorkflowModel::cancel_time_commit() noexcept
{
    if (time_pending()) operation_ = WorkflowOperation::IDLE;
}

bool RtcWorkflowModel::begin_ntp_toggle(bool desired) noexcept
{
    if (pending() || state_.dirty() || !state_.ntp_available()) return false;
    ntp_previous_ = state_.ntp_on();
    ntp_desired_ = desired;
    operation_ = WorkflowOperation::NTP_SET;
    return true;
}

void RtcWorkflowModel::finish_ntp_toggle(bool succeeded, int actual_status) noexcept
{
    if (!ntp_pending()) return;
    if (succeeded) {
        const int status = actual_status == 0 || actual_status == 1
                               ? actual_status
                               : (ntp_desired_ ? 1 : 0);
        state_.set_ntp_status(status);
    } else {
        state_.rollback_ntp(ntp_previous_);
    }
    operation_ = WorkflowOperation::IDLE;
}

void RtcWorkflowModel::cancel_ntp_toggle() noexcept
{
    if (!ntp_pending()) return;
    state_.rollback_ntp(ntp_previous_);
    operation_ = WorkflowOperation::IDLE;
}

RtcWorkflowModel &session() noexcept
{
    static RtcWorkflowModel instance;
    return instance;
}

} // namespace settings_rtc

} // namespace

void settings_rtc_ntp_api(int command, void *data) noexcept
{
    auto &state = settings_rtc::ntp_adapter_state();
    if (command == SettingApiReadFlag || command == SettingApiReadFlagTimeStart) {
        if (command == SettingApiReadFlagTimeStart) settings_rtc::refresh_ntp_cache();

        bool enabled = false;
        bool pending = false;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            enabled = state.enabled;
            pending = state.pending;
        }
        if (!data) return;

        if (command == SettingApiReadFlag) {
            *static_cast<bool *>(data) = enabled;
        } else {
            auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
            std::get<0>(*result) = enabled;
            if (std::get<1>(*result))
                std::get<1>(*result)->store(pending, std::memory_order_release);
        }
        return;
    }
    if (command != SettingApiActivate) return;

    auto &session = settings_rtc::session();
    bool desired = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.pending || !state.available) return;
        desired = !state.enabled;
        state.pending = true;
    }
    if (!session.begin_ntp_toggle(desired)) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pending = false;
        return;
    }

    const int result = settings_rtc::set_ntp_async(
        desired,
        [desired](settings_rtc::PrivilegedResult value) {
            settings_rtc::session().finish_ntp_toggle(value.succeeded(), desired ? 1 : 0);
            auto &state = settings_rtc::ntp_adapter_state();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.pending = false;
                if (value.succeeded()) {
                    state.available = true;
                    state.enabled = desired;
                }
                state.initialized = true;
            }
        });
    if (result != 0) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pending = false;
        session.cancel_ntp_toggle();
    }
}

namespace {

enum class LayoutMetric : int {
    StatusLabelW    = 232,
    StatusLabelX    = 84,
    StatusLabelY    = 118,
    StatusTextColor = 0xFF6666,
    StatusFontSize  = 10,
};

constexpr int metric(LayoutMetric value) noexcept
{
    return static_cast<int>(value);
}

} // namespace

struct LvSettingRtcPage3::Impl {
    struct ActionBackup {
        SettingEntry *entry = nullptr;
        SettingApiAsyncCallBackFunc async_api;
        SettingActivationPolicy policy = SettingActivationPolicy::LeaveImmediately;
    };

    std::vector<ActionBackup> actions;
    lv_obj_t *status_label = nullptr;
    std::string last_error;
    bool refresh_pending = false;
};

struct LvSettingRtcConfirmPage3::Impl {
    struct ActionBackup {
        SettingEntry *entry = nullptr;
        SettingApiAsyncCallBackFunc async_api;
        SettingActivationPolicy policy = SettingActivationPolicy::LeaveImmediately;
    };

    struct RequestState {
        std::atomic<std::uint64_t> request_id{0};
        std::atomic_bool cancelled{false};
        std::atomic_bool terminal{false};
    };

    std::function<void()> back_callback;
    std::vector<ActionBackup> actions;
    std::shared_ptr<RequestState> request_state;
    lv_obj_t *status_label = nullptr;
    std::string last_error;

    static void enqueue_outcome(const LvSettingValuePage3Base::ActivationSink &sink,
                                const std::shared_ptr<RequestState> &request,
                                settings_rtc::PrivilegedResultKind outcome) noexcept;
    static const char *error_message(settings_rtc::PrivilegedResultKind result) noexcept;
};

LvSettingRtcPage3::LvSettingRtcPage3() : impl_(std::make_unique<Impl>()) {}

LvSettingRtcPage3::LvSettingRtcPage3(lv_obj_t *parent, const NodeIter &parent_node)
    : LvSettingValuePage3Base(parent_node, {}), impl_(std::make_unique<Impl>())
{
    install_actions();
    initialize(parent);
    create_status_label();
    start_refresh();
}

LvSettingRtcPage3::LvSettingRtcPage3(lv_obj_t *parent,
                                     const NodeIter &parent_node,
                                     std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, std::move(back_callback)), impl_(std::make_unique<Impl>())
{
    install_actions();
    initialize(parent);
    create_status_label();
    start_refresh();
}

LvSettingRtcPage3::~LvSettingRtcPage3()
{
    cancel_async_tasks();
    restore_actions();
    impl_->status_label = nullptr;
}

const std::string &LvSettingRtcPage3::last_error() const noexcept { return impl_->last_error; }

int LvSettingRtcPage3::initial_selection() const
{
    settings_rtc::RtcField field = settings_rtc::RtcField::YEAR;
    if (!settings_rtc::RtcStateModel::field_from_name(parent_node()->label, field)) return 0;
    const int selection = settings_rtc::session().state().field_selection_index(field);
    return selection < 0 ? 0 : selection;
}

void LvSettingRtcPage3::install_actions()
{
    settings_rtc::RtcField field = settings_rtc::RtcField::YEAR;
    if (!settings_rtc::RtcStateModel::field_from_name(parent_node()->label, field)) return;

    for (auto child = parent_node().begin(); child != parent_node().end(); ++child) {
        impl_->actions.push_back({&*child, child->Async_api, child->activation_policy});
        child->Async_api = [this, field](int command, void *data) -> SettingApiResult {
            if (command != SettingApiActivate) return SettingApiResult::NotHandled;
            auto *value_page = static_cast<LvSettingValuePage3Base *>(data);
            if (!value_page || value_page->selected_index < 0) {
                set_error("Invalid RTC value");
                return SettingApiResult::Failure;
            }

            auto &workflow = settings_rtc::session();
            if (impl_->refresh_pending) {
                set_error("Reading RTC status");
                return SettingApiResult::Failure;
            }
            if (workflow.pending()) {
                set_error("RTC operation is pending");
                return SettingApiResult::Failure;
            }
            if (workflow.state().ntp_on()) {
                set_error("Disable NTP before editing time");
                return SettingApiResult::Failure;
            }
            if (!workflow.edit_field_selection(field,
                                               static_cast<std::size_t>(value_page->selected_index))) {
                set_error("Invalid calendar date");
                return SettingApiResult::Failure;
            }

            clear_error();
            return SettingApiResult::Success;
        };
        child->activation_policy = SettingActivationPolicy::WaitForResult;
    }
}

void LvSettingRtcPage3::restore_actions() noexcept
{
    for (const auto &backup : impl_->actions) {
        if (!backup.entry) continue;
        backup.entry->Async_api = backup.async_api;
        backup.entry->activation_policy = backup.policy;
    }
    impl_->actions.clear();
}

void LvSettingRtcPage3::create_status_label()
{
    if (!ComponensObj) return;
    impl_->status_label = lv_label_create(ComponensObj);
    if (!impl_->status_label) return;
    lv_obj_set_width(impl_->status_label, ::metric(::LayoutMetric::StatusLabelW));
    lv_obj_set_pos(impl_->status_label,
                   ::metric(::LayoutMetric::StatusLabelX),
                   ::metric(::LayoutMetric::StatusLabelY));
    lv_obj_set_style_text_color(
        impl_->status_label, lv_color_hex(::metric(::LayoutMetric::StatusTextColor)), LV_PART_MAIN);
    lv_obj_set_style_text_font(
        impl_->status_label,
        settings_fonts::sans(::metric(::LayoutMetric::StatusFontSize), LV_FREETYPE_FONT_STYLE_BOLD),
        LV_PART_MAIN);
    lv_label_set_text(impl_->status_label, "");
    lv_obj_add_flag(impl_->status_label, LV_OBJ_FLAG_HIDDEN);
}

void LvSettingRtcPage3::start_refresh()
{
    impl_->refresh_pending = true;
    if (!ensure_async_dispatch()) {
        impl_->refresh_pending = false;
        set_error("RTC refresh unavailable");
        return;
    }

    const auto token = async_token();
    const int start_result = settings_rtc::refresh_async(
        [this, token](settings_rtc::RefreshResult result) mutable {
            SettingsAsync::Dispatch::enqueue_from_callback(
                token,
                [this, token, result = std::move(result)]() mutable {
                    if (!token.valid() || !ComponensObj) return;
                    impl_->refresh_pending = false;
                    auto &workflow = settings_rtc::session();
                    workflow.set_ntp_status(result.ntp.status);
                    const bool time_loaded = result.time.valid && workflow.load_local_time(result.time.payload);
                    if (!result.ntp.available) set_error("NTP status unavailable");
                    else if (!time_loaded) set_error("RTC time unavailable");
                    else clear_error();
                    select(initial_selection());
                });
        });
    if (start_result != 0) {
        impl_->refresh_pending = false;
        set_error("Unable to read RTC status");
    }
}

void LvSettingRtcPage3::set_error(const char *message)
{
    impl_->last_error = message ? message : "RTC operation failed";
    if (impl_->status_label) {
        lv_label_set_text(impl_->status_label, impl_->last_error.c_str());
        lv_obj_clear_flag(impl_->status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void LvSettingRtcPage3::clear_error()
{
    impl_->last_error.clear();
    if (impl_->status_label) {
        lv_label_set_text(impl_->status_label, "");
        lv_obj_add_flag(impl_->status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

LvSettingRtcConfirmPage3::LvSettingRtcConfirmPage3() : impl_(std::make_unique<Impl>()) {}

LvSettingRtcConfirmPage3::LvSettingRtcConfirmPage3(lv_obj_t *parent,
                                                   const NodeIter &parent_node,
                                                   std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {}), impl_(std::make_unique<Impl>())
{
    impl_->back_callback = std::move(back_callback);
    LeaveSelfPage = [this] { leave_page(); };
    install_actions();
    initialize(parent);
    create_status_label();
}

LvSettingRtcConfirmPage3::~LvSettingRtcConfirmPage3()
{
    cancel_backend_request();
    cancel_async_tasks();
    restore_actions();
    impl_->status_label = nullptr;
}

const std::string &LvSettingRtcConfirmPage3::last_error() const noexcept { return impl_->last_error; }

int LvSettingRtcConfirmPage3::initial_selection() const { return 1; }

void LvSettingRtcConfirmPage3::install_actions()
{
    for (auto child = parent_node().begin(); child != parent_node().end(); ++child) {
        impl_->actions.push_back({&*child, child->Async_api, child->activation_policy});
        const bool save = child->label == "Yes";
        child->Async_api = [this, save](int command, void *) -> SettingApiResult {
            if (command != SettingApiActivate) return SettingApiResult::NotHandled;
            return save ? begin_save() : discard_and_leave();
        };
        child->activation_policy = SettingActivationPolicy::WaitForResult;
    }
}

void LvSettingRtcConfirmPage3::restore_actions() noexcept
{
    for (const auto &backup : impl_->actions) {
        if (!backup.entry) continue;
        backup.entry->Async_api = backup.async_api;
        backup.entry->activation_policy = backup.policy;
    }
    impl_->actions.clear();
}

SettingApiResult LvSettingRtcConfirmPage3::discard_and_leave()
{
    if (activation_pending()) return SettingApiResult::Failure;
    cancel_backend_request();
    settings_rtc::session().discard_edits();
    clear_error();
    return SettingApiResult::Success;
}

SettingApiResult LvSettingRtcConfirmPage3::begin_save()
{
    auto &workflow = settings_rtc::session();
    const auto eligibility = workflow.commit_eligibility();
    if (eligibility == settings_rtc::CommitEligibility::NTP_ENABLED) {
        set_error("Disable NTP before writing RTC");
        return SettingApiResult::Failure;
    }
    if (eligibility == settings_rtc::CommitEligibility::NO_EDITS) {
        set_error("No time changes to save");
        return SettingApiResult::Failure;
    }
    if (!workflow.begin_time_commit()) {
        set_error("RTC write is already pending");
        return SettingApiResult::Failure;
    }

    auto request = std::make_shared<Impl::RequestState>();
    impl_->request_state = request;
    const auto sink = activation_sink();
    const std::string timestamp = workflow.state().timestamp();
    const int start_result = settings_rtc::set_time_async(
        timestamp,
        [sink, request](settings_rtc::PrivilegedResult result) {
            Impl::enqueue_outcome(sink, request, result.kind);
        },
        [sink, request](int start_code, std::uint64_t request_id) {
            if (start_code != 0) {
                Impl::enqueue_outcome(sink, request, settings_rtc::PrivilegedResultKind::EXEC_FAILED);
                return;
            }

            request->request_id.store(request_id, std::memory_order_release);
            if (request->cancelled.load(std::memory_order_acquire)) {
                const std::uint64_t active_request = request->request_id.exchange(0, std::memory_order_acq_rel);
                if (active_request != 0) settings_rtc::cancel_request(active_request);
            }
        });

    if (start_result != 0) {
        workflow.finish_time_commit(false);
        impl_->request_state.reset();
        set_error("Unable to start RTC write");
        return SettingApiResult::Failure;
    }
    clear_error();
    return SettingApiResult::Pending;
}

void LvSettingRtcConfirmPage3::Impl::enqueue_outcome(
    const LvSettingValuePage3Base::ActivationSink &sink,
    const std::shared_ptr<RequestState> &request,
    settings_rtc::PrivilegedResultKind outcome) noexcept
{
    if (request->terminal.exchange(true, std::memory_order_acq_rel)) return;
    if (!sink.valid()) return;

    const bool queued = SettingsAsync::Dispatch::enqueue_from_callback(
        sink.dispatch_token,
        [sink, request, outcome] {
            if (!sink.valid()) return;
            auto *page = static_cast<LvSettingRtcConfirmPage3 *>(sink.owner);
            const bool succeeded = outcome == settings_rtc::PrivilegedResultKind::SUCCESS;
            settings_rtc::session().finish_time_commit(succeeded);
            if (page) {
                request->request_id.store(0, std::memory_order_release);
                if (!succeeded) page->set_error(Impl::error_message(outcome));
                else page->clear_error();
                if (page->impl_->request_state == request) page->impl_->request_state.reset();
            }
            page->finish_activation(
                sink,
                succeeded ? SettingApiResult::Success
                          : (outcome == settings_rtc::PrivilegedResultKind::CANCELLED
                                 ? SettingApiResult::Cancelled
                                 : SettingApiResult::Failure));
        });
    if (!queued) settings_rtc::session().cancel_time_commit();
}

const char *LvSettingRtcConfirmPage3::Impl::error_message(settings_rtc::PrivilegedResultKind result) noexcept
{
    switch (result) {
    case settings_rtc::PrivilegedResultKind::AUTH_FAILED: return "RTC authentication failed";
    case settings_rtc::PrivilegedResultKind::CANCELLED: return "RTC write cancelled";
    case settings_rtc::PrivilegedResultKind::TIMED_OUT: return "RTC write timed out";
    case settings_rtc::PrivilegedResultKind::EXEC_FAILED: return "RTC write failed";
    case settings_rtc::PrivilegedResultKind::SUCCESS: break;
    }
    return "RTC write failed";
}

void LvSettingRtcConfirmPage3::create_status_label()
{
    if (!ComponensObj) return;
    impl_->status_label = lv_label_create(ComponensObj);
    if (!impl_->status_label) return;
    lv_obj_set_width(impl_->status_label, ::metric(::LayoutMetric::StatusLabelW));
    lv_obj_set_pos(impl_->status_label,
                   ::metric(::LayoutMetric::StatusLabelX),
                   ::metric(::LayoutMetric::StatusLabelY));
    lv_obj_set_style_text_color(
        impl_->status_label, lv_color_hex(::metric(::LayoutMetric::StatusTextColor)), LV_PART_MAIN);
    lv_obj_set_style_text_font(
        impl_->status_label,
        settings_fonts::sans(::metric(::LayoutMetric::StatusFontSize), LV_FREETYPE_FONT_STYLE_BOLD),
        LV_PART_MAIN);
    lv_label_set_text(impl_->status_label, "");
    lv_obj_add_flag(impl_->status_label, LV_OBJ_FLAG_HIDDEN);
}

void LvSettingRtcConfirmPage3::set_error(const char *message)
{
    impl_->last_error = message ? message : "RTC write failed";
    if (impl_->status_label) {
        lv_label_set_text(impl_->status_label, impl_->last_error.c_str());
        lv_obj_clear_flag(impl_->status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void LvSettingRtcConfirmPage3::clear_error()
{
    impl_->last_error.clear();
    if (impl_->status_label) {
        lv_label_set_text(impl_->status_label, "");
        lv_obj_add_flag(impl_->status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void LvSettingRtcConfirmPage3::cancel_backend_request() noexcept
{
    if (impl_->request_state) {
        const auto request = impl_->request_state;
        request->cancelled.store(true, std::memory_order_release);
        request->terminal.store(true, std::memory_order_release);
        const std::uint64_t request_id = request->request_id.exchange(0, std::memory_order_acq_rel);
        if (request_id != 0) settings_rtc::cancel_request(request_id);
        impl_->request_state.reset();
    }
    settings_rtc::session().cancel_time_commit();
}

void LvSettingRtcConfirmPage3::leave_page()
{
    cancel_backend_request();
    settings_rtc::session().discard_edits();
    if (impl_->back_callback) impl_->back_callback();
}

std::unique_ptr<DComponens::LvglComponensBase> settings_rtc_page_factory(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingRtcPage3>(parent, parent_node, std::move(back_callback));
}

std::unique_ptr<DComponens::LvglComponensBase> settings_rtc_confirm_page_factory(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingRtcConfirmPage3>(parent, parent_node, std::move(back_callback));
}
