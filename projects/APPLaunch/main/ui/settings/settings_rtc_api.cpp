#include "settings_rtc_api.hpp"

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

namespace setting {
namespace {

constexpr unsigned kFieldCount = static_cast<unsigned>(RtcField::COUNT);

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
    for (unsigned index = 0; index < kFieldCount; ++index) {
        const std::size_t separator = payload.find(',', start);
        const std::size_t end = separator == std::string_view::npos ? payload.size() : separator;
        if (!parse_fixed_decimal(payload.substr(start, end - start), values[index])) return false;
        if (index + 1 == kFieldCount)
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
    return index < kFieldCount ? minimums[index] : 0;
}

int RtcStateModel::field_max(RtcField field) const noexcept
{
    static constexpr int maximums[] = {2099, 12, 31, 23, 59, 59};
    const unsigned index = field_index(field);
    if (field == RtcField::DAY)
        return days_in_month(values_[field_index(RtcField::YEAR)], values_[field_index(RtcField::MONTH)]);
    return index < kFieldCount ? maximums[index] : 0;
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
    if (index >= kFieldCount) return options;

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
    return index < kFieldCount ? names[index] : std::string_view{};
}

bool RtcStateModel::field_from_index(int index, RtcField &field) noexcept
{
    if (index < 0 || index >= static_cast<int>(kFieldCount)) return false;
    field = static_cast<RtcField>(index);
    return true;
}

bool RtcStateModel::field_from_name(std::string_view name, RtcField &field) noexcept
{
    for (unsigned index = 0; index < kFieldCount; ++index) {
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
        if (state.pending) return;
        state.pending = true;
    }
    const int result = read_ntp_async([](NtpReadResult value) {
        auto &state = ntp_adapter_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pending = false;
        state.available = value.available;
        if (value.available) state.enabled = value.enabled;
    });
    if (result != 0) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pending = false;
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
    if (!result.valid && result.code == 0) result.code = kApiErrorInvalidPayload;
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
        invoke_noexcept(callback, make_time_result(kApiErrorInvocation, {}));
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
    if (!result.valid) result.code = kApiErrorInvalidPayload;
    invoke_noexcept(callback, std::move(result));
}

int submit_privileged(std::list<std::string> arguments,
                      PrivilegedCallback callback,
                      RequestStartedCallback started)
{
    if (!callback) return kApiErrorInvalidArgument;
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
        return kApiErrorInvocation;
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
    if (!callback) return kApiErrorInvalidArgument;

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
    if (!callback) return kApiErrorInvalidArgument;

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
    if (!callback) return kApiErrorInvalidArgument;

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

    return ntp_start != 0 && time_start != 0 ? kApiErrorInvocation : 0;
}

int set_ntp_async(bool enabled, PrivilegedCallback callback, RequestStartedCallback started)
{
    return submit_privileged({"NtpSet", enabled ? "1" : "0"}, std::move(callback), std::move(started));
}

int set_time_async(std::string timestamp, PrivilegedCallback callback, RequestStartedCallback started)
{
    if (!callback) return kApiErrorInvalidArgument;
    RtcValues parsed{};
    if (!RtcStateModel::parse_timestamp(timestamp, parsed)) return kApiErrorInvalidArgument;
    return submit_privileged({"TimeSet", std::move(timestamp)}, std::move(callback), std::move(started));
}

void update_ntp_cache(int status) noexcept
{
    auto &state = ntp_adapter_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.available = status == 0 || status == 1;
    if (state.available) state.enabled = status == 1;
}

void settings_rtc_ntp_api(int command, void *data) noexcept
{
    auto &state = ntp_adapter_state();
    if (command == 0 || command == 2) {
        if (command == 2) refresh_ntp_cache();
        bool enabled = false;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            enabled = state.enabled;
        }
        if (data) {
            if (command == 0) {
                *static_cast<bool *>(data) = enabled;
            } else {
                auto *result = static_cast<std::tuple<bool, std::atomic_bool *> *>(data);
                std::get<0>(*result) = enabled;
                if (std::get<1>(*result))
                    std::get<1>(*result)->store(false, std::memory_order_release);
            }
        }
        return;
    }
    if (command != 1) return;
    if (session().state().dirty() || session().pending()) return;

    bool desired = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.pending || !state.available) return;
        desired = !state.enabled;
        state.pending = true;
    }

    const int result = set_ntp_async(
        desired,
        [desired](PrivilegedResult value) {
            auto &state = ntp_adapter_state();
            bool should_refresh = false;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.pending = false;
                if (value.succeeded()) {
                    state.available = true;
                    state.enabled = desired;
                    should_refresh = true;
                }
            }
            if (should_refresh) refresh_ntp_cache();
        });
    if (result != 0) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.pending = false;
    }
}

int cancel_request(std::uint64_t request_id)
{
    if (request_id == 0) return kApiErrorInvalidArgument;
    try {
        cp0_signal_sudo_cancel(request_id, [](int) {});
    } catch (...) {
        return kApiErrorInvocation;
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
