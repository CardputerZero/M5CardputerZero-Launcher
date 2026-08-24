#include "rtc_state_model.hpp"

#include <cstdio>

namespace setting {

namespace {

constexpr unsigned field_index(RtcField field)
{
    return static_cast<unsigned>(field);
}

} // namespace

RtcConfirmAction RtcWriteConfirmModel::handle(RtcConfirmInput input)
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

RtcOverlayLifecycleModel::Token RtcOverlayLifecycleModel::open()
{
    if (active_) return 0;
    active_ = true;
    if (++generation_ == 0) ++generation_;
    return generation_;
}

bool RtcOverlayLifecycleModel::close(Token token)
{
    if (!active_ || token == 0 || token != generation_) return false;
    active_ = false;
    return true;
}

void RtcStateModel::set_ntp_status(int status)
{
    ntp_available_ = status >= 0;
    if (ntp_available_)
        ntp_on_ = status == 1;
}

void RtcStateModel::rollback_ntp(bool previous)
{
    ntp_on_ = previous;
}

NtpToggleEligibility RtcStateModel::ntp_toggle_eligibility(bool in_flight) const
{
    if (in_flight) return NtpToggleEligibility::IN_FLIGHT;
    if (dirty_) return NtpToggleEligibility::DIRTY;
    if (!ntp_available_) return NtpToggleEligibility::UNAVAILABLE;
    return NtpToggleEligibility::ALLOWED;
}

bool RtcStateModel::load_local_time(const std::string &csv)
{
    Values parsed{};
    char trailing = '\0';
    if (std::sscanf(csv.c_str(), "%d,%d,%d,%d,%d,%d %c", &parsed[0], &parsed[1],
            &parsed[2], &parsed[3], &parsed[4], &parsed[5], &trailing) != 6 || !is_valid(parsed))
        return false;

    values_ = parsed;
    dirty_ = false;
    return true;
}

int RtcStateModel::field_min(RtcField field) const
{
    static constexpr int minimums[] = {2000, 1, 1, 0, 0, 0};
    const unsigned index = field_index(field);
    return index < field_index(RtcField::COUNT) ? minimums[index] : 0;
}

int RtcStateModel::field_max(RtcField field) const
{
    static constexpr int maximums[] = {2099, 12, 31, 23, 59, 59};
    const unsigned index = field_index(field);
    if (field == RtcField::DAY)
        return days_in_month(values_[field_index(RtcField::YEAR)], values_[field_index(RtcField::MONTH)]);
    return index < field_index(RtcField::COUNT) ? maximums[index] : 0;
}

int RtcStateModel::field_value(RtcField field) const
{
    const unsigned index = field_index(field);
    return index < values_.size() ? values_[index] : 0;
}

bool RtcStateModel::edit_field(RtcField field, int value)
{
    const unsigned index = field_index(field);
    if (index >= values_.size() || value < field_min(field) || value > field_max(field))
        return false;

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
    if (field_index(field) >= field_index(RtcField::COUNT))
        return options;
    const int minimum = field_min(field);
    const int maximum = field_max(field);
    if (maximum < minimum)
        return options;
    options.reserve(static_cast<std::size_t>(maximum - minimum + 1));
    for (int value = minimum; value <= maximum; ++value)
        options.push_back(std::to_string(value));
    return options;
}

int RtcStateModel::field_selection_index(RtcField field) const
{
    const unsigned index = field_index(field);
    if (index >= values_.size())
        return -1;
    return field_value(field) - field_min(field);
}

bool RtcStateModel::edit_field_selection(RtcField field, std::size_t selection)
{
    const int minimum = field_min(field);
    const int maximum = field_max(field);
    if (maximum < minimum || selection > static_cast<std::size_t>(maximum - minimum))
        return false;
    return edit_field(field, minimum + static_cast<int>(selection));
}

std::string RtcStateModel::timestamp() const
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
        values_[0], values_[1], values_[2], values_[3], values_[4], values_[5]);
    return buffer;
}

std::list<std::string> RtcStateModel::commit_request() const
{
    return {"TimeSet", timestamp()};
}

std::string_view RtcStateModel::field_name(RtcField field)
{
    static constexpr std::string_view names[] = {
        "Year", "Month", "Day", "Hour", "Minute", "Second",
    };
    const unsigned index = field_index(field);
    return index < field_index(RtcField::COUNT) ? names[index] : std::string_view{};
}

bool RtcStateModel::field_from_index(int index, RtcField &field)
{
    if (index < 0 || index >= static_cast<int>(RtcField::COUNT))
        return false;
    field = static_cast<RtcField>(index);
    return true;
}

int RtcStateModel::days_in_month(int year, int month)
{
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month != 2) return days[month - 1];
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return leap ? 29 : 28;
}

bool RtcStateModel::is_valid(const Values &values)
{
    if (values[0] < 2000 || values[0] > 2099 || values[1] < 1 || values[1] > 12)
        return false;
    return values[2] >= 1 && values[2] <= days_in_month(values[0], values[1]) &&
        values[3] >= 0 && values[3] <= 23 && values[4] >= 0 && values[4] <= 59 &&
        values[5] >= 0 && values[5] <= 59;
}

PrivilegedResultKind classify_privileged_result(int result)
{
    switch (result) {
    case 0: return PrivilegedResultKind::SUCCESS;
    case 1: return PrivilegedResultKind::AUTH_FAILED;
    case 3: return PrivilegedResultKind::CANCELLED;
    case 4: return PrivilegedResultKind::TIMED_OUT;
    default: return PrivilegedResultKind::EXEC_FAILED;
    }
}

} // namespace setting
