#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <vector>

namespace setting {

template <typename EventTarget, typename CurrentTarget, typename TrackedOverlay>
constexpr bool rtc_overlay_delete_callback_allowed(EventTarget event_target,
                                                   CurrentTarget current_target,
                                                   TrackedOverlay tracked_overlay) noexcept
{
    return event_target && event_target == current_target &&
           event_target == tracked_overlay;
}

enum class RtcField { YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, COUNT };

enum class NtpToggleEligibility { ALLOWED, IN_FLIGHT, DIRTY, UNAVAILABLE };

enum class PrivilegedResultKind { SUCCESS, AUTH_FAILED, CANCELLED, TIMED_OUT, EXEC_FAILED };

enum class RtcConfirmInput { SELECT_SAVE, SELECT_DISCARD, CONFIRM, CANCEL };
enum class RtcConfirmAction { NONE, SAVE, DISCARD };

class RtcWriteConfirmModel
{
public:
    void reset() { save_selected_ = false; }
    bool save_selected() const { return save_selected_; }
    RtcConfirmAction handle(RtcConfirmInput input);

private:
    bool save_selected_ = false;
};

class RtcOverlayLifecycleModel
{
public:
    using Token = std::uint64_t;

    Token open();
    bool close(Token token);
    bool active() const { return active_; }

private:
    bool active_ = false;
    Token generation_ = 0;
};

class RtcStateModel
{
public:
    using Values = std::array<int, static_cast<unsigned>(RtcField::COUNT)>;

    const Values &values() const { return values_; }
    bool dirty() const { return dirty_; }
    bool ntp_on() const { return ntp_on_; }
    bool ntp_available() const { return ntp_available_; }

    void set_ntp_status(int status);
    void rollback_ntp(bool previous);
    NtpToggleEligibility ntp_toggle_eligibility(bool in_flight) const;

    bool load_local_time(const std::string &csv);
    int field_min(RtcField field) const;
    int field_max(RtcField field) const;
    int field_value(RtcField field) const;
    bool edit_field(RtcField field, int value);
    std::vector<std::string> field_options(RtcField field) const;
    int field_selection_index(RtcField field) const;
    bool edit_field_selection(RtcField field, std::size_t selection);

    void discard_edits() { dirty_ = false; }
    void finish_commit(bool succeeded) { dirty_ = !succeeded; }
    std::string timestamp() const;
    std::list<std::string> commit_request() const;

    static int days_in_month(int year, int month);
    static std::string_view field_name(RtcField field);
    static bool field_from_index(int index, RtcField &field);

private:
    static bool is_valid(const Values &values);

    Values values_ = {2026, 1, 1, 0, 0, 0};
    bool ntp_on_ = true;
    bool ntp_available_ = true;
    bool dirty_ = false;
};

PrivilegedResultKind classify_privileged_result(int result);

} // namespace setting
