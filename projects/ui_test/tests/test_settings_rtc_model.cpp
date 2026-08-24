#include "settings_rtc_api.hpp"

#include <cassert>
#include <list>

int main()
{
    using namespace settings_rtc;

    RtcStateModel model;
    assert(model.timestamp() == "2026-01-01 00:00:00");
    assert(!model.dirty());

    assert(RtcStateModel::days_in_month(2000, 2) == 29);
    assert(RtcStateModel::days_in_month(2024, 2) == 29);
    assert(RtcStateModel::days_in_month(2100, 2) == 28);
    assert(RtcStateModel::days_in_month(2025, 4) == 30);
    assert(RtcStateModel::days_in_month(2025, 13) == 0);

    assert(model.load_local_time("2024,2,29,23,59,59"));
    assert(model.timestamp() == "2024-02-29 23:59:59");
    assert(model.field_max(RtcField::DAY) == 29);
    assert(model.load_local_time("2025-01-01 00:00:00"));
    assert(model.timestamp() == "2025-01-01 00:00:00");
    assert(!model.dirty());

    assert(!model.load_local_time("2023,2,29,12,0,0"));
    assert(!model.load_local_time("2024,1,1,24,0,0"));
    assert(!model.load_local_time("2024,1,1,0,0,0 trailing"));
    assert(!model.load_local_time("2024-02-29 23:59:60"));
    assert(model.timestamp() == "2025-01-01 00:00:00");

    assert(model.edit_field(RtcField::YEAR, 2024));
    assert(model.edit_field(RtcField::MONTH, 2));
    assert(model.edit_field(RtcField::DAY, 29));
    assert(model.edit_field(RtcField::YEAR, 2023));
    assert(model.field_value(RtcField::DAY) == 28);
    assert(!model.edit_field(RtcField::DAY, 29));
    assert(!model.edit_field(RtcField::HOUR, 24));
    assert(model.field_options(RtcField::DAY).size() == 28);
    assert(model.field_selection_index(RtcField::DAY) == 27);
    assert(!model.edit_field_selection(RtcField::DAY, 28));
    assert(model.commit_request() ==
           (std::list<std::string>{"TimeSet", "2023-02-28 00:00:00"}));

    RtcField field = RtcField::YEAR;
    assert(RtcStateModel::field_from_name("Second", field));
    assert(field == RtcField::SECOND);
    assert(!RtcStateModel::field_from_name("Unknown", field));
    assert(RtcStateModel::field_from_index(5, field));
    assert(field == RtcField::SECOND);
    assert(!RtcStateModel::field_from_index(6, field));

    model.discard_edits();
    model.set_ntp_status(-1);
    assert(!model.ntp_available());
    assert(model.ntp_toggle_eligibility(false) == NtpToggleEligibility::UNAVAILABLE);
    model.set_ntp_status(0);
    assert(model.ntp_available() && !model.ntp_on());
    assert(model.ntp_toggle_eligibility(true) == NtpToggleEligibility::IN_FLIGHT);
    assert(model.ntp_toggle_eligibility(false) == NtpToggleEligibility::ALLOWED);
    assert(model.edit_field(RtcField::SECOND, 1));
    assert(model.ntp_toggle_eligibility(false) == NtpToggleEligibility::DIRTY);
    model.discard_edits();
    assert(model.ntp_toggle_eligibility(false) == NtpToggleEligibility::ALLOWED);

    RtcWorkflowModel workflow;
    workflow.set_ntp_status(0);
    assert(workflow.commit_eligibility() == CommitEligibility::NO_EDITS);
    assert(workflow.edit_field(RtcField::MINUTE, 1));
    assert(workflow.commit_eligibility() == CommitEligibility::ALLOWED);
    assert(workflow.begin_time_commit());
    assert(!workflow.begin_time_commit());
    assert(workflow.time_pending());
    workflow.cancel_time_commit();
    assert(!workflow.pending());
    assert(workflow.state().dirty());
    assert(workflow.begin_time_commit());
    workflow.finish_time_commit(false);
    assert(workflow.state().dirty());
    assert(workflow.begin_time_commit());
    workflow.finish_time_commit(true);
    assert(!workflow.state().dirty());

    assert(workflow.begin_ntp_toggle(true));
    assert(workflow.ntp_pending());
    assert(!workflow.begin_ntp_toggle(false));
    workflow.finish_ntp_toggle(true, 1);
    assert(workflow.state().ntp_on());
    assert(workflow.commit_eligibility() == CommitEligibility::NTP_ENABLED);

    RtcWriteConfirmModel confirm;
    assert(confirm.handle(RtcConfirmInput::CONFIRM) == RtcConfirmAction::DISCARD);
    assert(confirm.handle(RtcConfirmInput::SELECT_SAVE) == RtcConfirmAction::NONE);
    assert(confirm.handle(RtcConfirmInput::CONFIRM) == RtcConfirmAction::SAVE);
    assert(confirm.handle(RtcConfirmInput::CANCEL) == RtcConfirmAction::DISCARD);

    RtcOverlayLifecycleModel overlay;
    const auto first = overlay.open();
    assert(first != 0);
    assert(overlay.open() == 0);
    assert(!overlay.close(first + 1));
    assert(overlay.close(first));
    const auto second = overlay.open();
    assert(second != 0 && second != first);
    assert(overlay.close(second));

    assert(classify_privileged_result(0) == PrivilegedResultKind::SUCCESS);
    assert(classify_privileged_result(1) == PrivilegedResultKind::AUTH_FAILED);
    assert(classify_privileged_result(3) == PrivilegedResultKind::CANCELLED);
    assert(classify_privileged_result(4) == PrivilegedResultKind::TIMED_OUT);
    assert(classify_privileged_result(99) == PrivilegedResultKind::EXEC_FAILED);
}
