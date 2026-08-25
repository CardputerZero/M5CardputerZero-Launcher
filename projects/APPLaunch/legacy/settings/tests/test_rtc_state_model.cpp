#include "../main/ui/model/rtc_state_model.hpp"

#include <cassert>
#include <list>
#include <vector>

int main()
{
    using namespace setting;

    int tracked_overlay_handle = 0;
    int stale_overlay_handle = 0;
    static_assert(noexcept(rtc_overlay_delete_callback_allowed(
        &tracked_overlay_handle, &tracked_overlay_handle, &tracked_overlay_handle)));
    assert(rtc_overlay_delete_callback_allowed(
        &tracked_overlay_handle, &tracked_overlay_handle, &tracked_overlay_handle));
    assert(!rtc_overlay_delete_callback_allowed(
        &tracked_overlay_handle, &stale_overlay_handle, &tracked_overlay_handle));
    assert(!rtc_overlay_delete_callback_allowed(
        &stale_overlay_handle, &stale_overlay_handle, &tracked_overlay_handle));
    assert(!rtc_overlay_delete_callback_allowed(
        static_cast<int *>(nullptr), &tracked_overlay_handle, &tracked_overlay_handle));

    RtcStateModel model;
    assert(model.timestamp() == "2026-01-01 00:00:00");
    assert(!model.dirty());

    assert(RtcStateModel::days_in_month(2000, 2) == 29);
    assert(RtcStateModel::days_in_month(2024, 2) == 29);
    assert(RtcStateModel::days_in_month(2100, 2) == 28);
    assert(RtcStateModel::days_in_month(2025, 2) == 28);
    assert(RtcStateModel::days_in_month(2025, 4) == 30);
    assert(RtcStateModel::days_in_month(2025, 13) == 0);

    assert(model.load_local_time("2024,2,29,23,59,58"));
    assert(model.field_value(RtcField::DAY) == 29);
    assert(model.field_max(RtcField::DAY) == 29);
    assert(!model.load_local_time("2023,2,29,12,0,0"));
    assert(!model.load_local_time("2024,1,1,24,0,0"));
    assert(!model.load_local_time("2024,1,1,0,0,0 trailing"));
    assert(model.timestamp() == "2024-02-29 23:59:58");
    assert(RtcStateModel::field_name(RtcField::YEAR) == "Year");
    assert(RtcStateModel::field_name(RtcField::SECOND) == "Second");
    assert(RtcStateModel::field_name(RtcField::COUNT).empty());
    assert(model.field_options(RtcField::COUNT).empty());
    assert(model.field_selection_index(RtcField::COUNT) == -1);
    assert(!model.edit_field_selection(RtcField::COUNT, 0));
    RtcField parsed_field = RtcField::YEAR;
    assert(RtcStateModel::field_from_index(5, parsed_field));
    assert(parsed_field == RtcField::SECOND);
    assert(!RtcStateModel::field_from_index(-1, parsed_field));
    assert(!RtcStateModel::field_from_index(6, parsed_field));

    const auto days = model.field_options(RtcField::DAY);
    assert(days.size() == 29);
    assert(days.front() == "1" && days.back() == "29");
    assert(model.field_selection_index(RtcField::DAY) == 28);
    assert(!model.edit_field_selection(RtcField::DAY, days.size()));
    assert(model.edit_field_selection(RtcField::DAY, 0));
    assert(model.field_value(RtcField::DAY) == 1);
    assert(model.edit_field_selection(RtcField::DAY, 28));
    assert(model.field_value(RtcField::DAY) == 29);

    assert(model.edit_field(RtcField::YEAR, 2023));
    assert(model.field_value(RtcField::DAY) == 28);
    assert(model.dirty());
    assert(!model.edit_field(RtcField::MONTH, 0));
    assert(!model.edit_field(RtcField::HOUR, 24));
    assert(model.field_value(RtcField::MONTH) == 2);
    assert(model.edit_field(RtcField::MONTH, 4));
    assert(model.edit_field(RtcField::DAY, 30));
    assert(!model.edit_field(RtcField::DAY, 31));
    assert(model.edit_field(RtcField::SECOND, 0));
    assert(model.timestamp() == "2023-04-30 23:59:00");
    assert(!model.load_local_time("invalid"));
    assert(model.dirty());
    assert(model.timestamp() == "2023-04-30 23:59:00");
    assert(model.commit_request() ==
        (std::list<std::string>{"TimeSet", "2023-04-30 23:59:00"}));

    model.finish_commit(false);
    assert(model.dirty());
    model.finish_commit(true);
    assert(!model.dirty());

    model.set_ntp_status(-1);
    assert(!model.ntp_available());
    assert(model.ntp_on());
    assert(model.ntp_toggle_eligibility(false) == NtpToggleEligibility::UNAVAILABLE);
    model.set_ntp_status(0);
    assert(model.ntp_available() && !model.ntp_on());
    assert(model.ntp_toggle_eligibility(false) == NtpToggleEligibility::ALLOWED);
    assert(model.ntp_toggle_eligibility(true) == NtpToggleEligibility::IN_FLIGHT);
    assert(model.edit_field(RtcField::MINUTE, 1));
    assert(model.ntp_toggle_eligibility(false) == NtpToggleEligibility::DIRTY);
    model.rollback_ntp(true);
    assert(model.ntp_on());

    assert(classify_privileged_result(0) == PrivilegedResultKind::SUCCESS);
    assert(classify_privileged_result(1) == PrivilegedResultKind::AUTH_FAILED);
    assert(classify_privileged_result(2) == PrivilegedResultKind::EXEC_FAILED);
    assert(classify_privileged_result(3) == PrivilegedResultKind::CANCELLED);
    assert(classify_privileged_result(4) == PrivilegedResultKind::TIMED_OUT);
    assert(classify_privileged_result(99) == PrivilegedResultKind::EXEC_FAILED);

    RtcWriteConfirmModel confirm;
    assert(!confirm.save_selected());
    assert(confirm.handle(RtcConfirmInput::CONFIRM) == RtcConfirmAction::DISCARD);
    assert(confirm.handle(RtcConfirmInput::SELECT_SAVE) == RtcConfirmAction::NONE);
    assert(confirm.save_selected());
    assert(confirm.handle(RtcConfirmInput::CONFIRM) == RtcConfirmAction::SAVE);
    confirm.reset();
    assert(confirm.handle(RtcConfirmInput::CANCEL) == RtcConfirmAction::DISCARD);

    RtcOverlayLifecycleModel overlay;
    assert(!overlay.active());
    const auto first_overlay = overlay.open();
    assert(first_overlay);
    assert(overlay.active());
    assert(!overlay.open());
    assert(overlay.close(first_overlay));
    assert(!overlay.active());
    assert(!overlay.close(first_overlay));
    const auto second_overlay = overlay.open();
    assert(second_overlay && second_overlay != first_overlay);
    assert(!overlay.close(first_overlay));
    assert(overlay.active());
    assert(overlay.close(second_overlay));
}
