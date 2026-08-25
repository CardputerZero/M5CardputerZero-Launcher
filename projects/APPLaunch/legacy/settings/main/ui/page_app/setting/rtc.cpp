#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

#include <memory>
#include <utility>

namespace setting {

void RTC::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    UISetupPage *page = &p;
    RTC *controller = this;
    MenuItem m;
    m.label = "RTC";
    m.sub_items = {
        {"NTP", true, true, [controller, page]() { controller->toggle_ntp(*page); }},
        {"Year", false, false, [controller, page]() { controller->enter_adjust(*page, 0); }},
        {"Month", false, false, [controller, page]() { controller->enter_adjust(*page, 1); }},
        {"Day", false, false, [controller, page]() { controller->enter_adjust(*page, 2); }},
        {"Hour", false, false, [controller, page]() { controller->enter_adjust(*page, 3); }},
        {"Minute", false, false, [controller, page]() { controller->enter_adjust(*page, 4); }},
        {"Second", false, false, [controller, page]() { controller->enter_adjust(*page, 5); }},
    };
    m.on_enter = [controller, page]() { controller->refresh_values(*page); };
    menu.push_back(m);
}

RTC::RTC() = default;

RTC::~RTC()
{
    shutdown();
}

void RTC::shutdown()
{
    const uint64_t request_id = async_operation_.shutdown();
    if (request_id)
        cp0_signal_sudo_cancel(request_id, nullptr);
    close_write_confirm();
}

void RTC::update_labels(UISetupPage &page)
{
    MenuItem *menu = SetupPageAccess(page).find_menu("RTC");
    if (menu && menu->sub_items.size() >= 7) {
        menu->sub_items[0].toggle_state = state_.ntp_on();
        char buf[32];
        for (int i = 0; i < 6; ++i) {
            const auto field = static_cast<RtcField>(i);
            snprintf(buf, sizeof(buf), "%.*s: %d",
                static_cast<int>(RtcStateModel::field_name(field).size()),
                RtcStateModel::field_name(field).data(), state_.field_value(field));
            menu->sub_items[i + 1].label = buf;
        }
    }
}

void RTC::refresh_values(UISetupPage &page)
{
    int ntp = -1;
    try {
        cp0_signal_osinfo_api({"NtpGet"}, [&](int code, std::string) { ntp = code; });
    } catch (...) {
        ntp = -1;
    }
    state_.set_ntp_status(ntp);
    RtcStateModel refreshed = state_;
    bool time_loaded = false;
    try {
        cp0_signal_osinfo_api({"LocalTime"}, [&](int code, std::string data) {
            if (code == 0) time_loaded = refreshed.load_local_time(data);
        });
    } catch (...) {
        time_loaded = false;
    }
    if (time_loaded) state_ = std::move(refreshed);
    update_labels(page);
}

void RTC::toggle_ntp(UISetupPage &page)
{
    NtpToggleEligibility eligibility = state_.ntp_toggle_eligibility(async_operation_.active());
    if (eligibility == NtpToggleEligibility::IN_FLIGHT)
        return;
    if (eligibility == NtpToggleEligibility::DIRTY) {
        update_labels(page);
        show_status("NTP unchanged", "Save or discard time edits first", Modal::ERROR);
        return;
    }
    if (eligibility == NtpToggleEligibility::UNAVAILABLE) {
        update_labels(page);
        show_status("NTP unavailable", "Unable to read NTP status", Modal::ERROR);
        return;
    }
    bool desired = state_.ntp_on();
    MenuItem *menu = SetupPageAccess(page).find_menu("RTC");
    if (menu && !menu->sub_items.empty()) desired = menu->sub_items[0].toggle_state;
    ntp_previous_ = state_.ntp_on();
    const auto operation = async_operation_.begin();
    if (!operation)
        return;
    show_status(desired ? "Enabling NTP" : "Disabling NTP", "Please wait...", Modal::BUSY);
    struct Context {
        RTC *rtc;
        UISetupPage *page;
        AsyncOperationLifecycle::Token operation;
        bool desired;
    };
    auto ctx = std::shared_ptr<Context>(
        new (std::nothrow) Context{this, &page, operation, desired});
    if (!ctx) {
        async_operation_.abort(operation);
        state_.rollback_ntp(ntp_previous_);
        update_labels(page);
        show_status(desired ? "Enable NTP failed" : "Disable NTP failed",
                    "Out of memory", Modal::ERROR);
        return;
    }
    uint64_t request_id = 0;
    int rc = -1;
    try {
        cp0_signal_system_admin_async({"NtpSet", desired ? "1" : "0"}, 60000, 30000,
        [ctx](int result_code, int exit_code) {
            cp0_sudo_result_t result = static_cast<cp0_sudo_result_t>(result_code);
            if (!ctx->operation.complete()) return;
            if (result != CP0_SUDO_RESULT_SUCCESS) {
                ctx->rtc->state_.rollback_ntp(ctx->rtc->ntp_previous_);
                ctx->rtc->update_labels(*ctx->page);
                if (result == CP0_SUDO_RESULT_CANCELLED) {
                    ctx->rtc->close_write_confirm();
                    SetupPageAccess(*ctx->page).build_sub_view();
                    return;
                }
                ctx->rtc->show_result_error(
                    result, exit_code, ctx->desired ? "Enable NTP" : "Disable NTP");
                return;
            }
            int actual = -1;
            try {
                cp0_signal_osinfo_api({"NtpGet"},
                                      [&](int code, std::string) { actual = code; });
            } catch (...) {
                actual = -1;
            }
            if (actual < 0) {
                ctx->rtc->state_.set_ntp_status(-1);
                ctx->rtc->show_status("NTP unavailable", "Unable to read NTP status", Modal::ERROR);
            } else {
                ctx->rtc->state_.set_ntp_status(actual);
                ctx->rtc->close_write_confirm();
            }
            ctx->rtc->update_labels(*ctx->page);
            SetupPageAccess(*ctx->page).build_sub_view();
        }, [&](int code, uint64_t id) { rc = code; request_id = id; });
    } catch (...) {
        rc = -1;
    }
    if (rc != 0) {
        async_operation_.abort(operation);
        state_.rollback_ntp(ntp_previous_);
        update_labels(page);
        show_status(desired ? "Enable NTP failed" : "Disable NTP failed",
                    "Unable to start request", Modal::ERROR);
    }
    else async_operation_.activate(operation, request_id);
}

void RTC::enter_adjust(UISetupPage &page, int field)
{
    if (state_.ntp_on())
        return;
    if (!RtcStateModel::field_from_index(field, field_))
        return;
    SetupPageAccess(page).enter_value(std::string(RtcStateModel::field_name(field_)),
        state_.field_options(field_), state_.field_selection_index(field_));
}

void RTC::apply_value(UISetupPage &page)
{
    const int selection = SetupPageAccess(page).value_selection();
    if (selection < 0 || !state_.edit_field_selection(field_, static_cast<std::size_t>(selection)))
        return;
    update_labels(page);
}

void RTC::commit_to_hardware(UISetupPage &page)
{
    const auto request = state_.commit_request();
    const auto operation = async_operation_.begin();
    if (!operation)
        return;
    show_status("Saving date & time", "Please wait...", Modal::BUSY);
    struct Context { RTC *rtc; UISetupPage *page; AsyncOperationLifecycle::Token operation; };
    auto ctx = std::shared_ptr<Context>(new (std::nothrow) Context{this, &page, operation});
    if (!ctx) {
        async_operation_.abort(operation);
        show_status("Save failed", "Out of memory", Modal::ERROR);
        return;
    }
    uint64_t request_id = 0;
    int rc = -1;
    try {
        cp0_signal_system_admin_async(request, 60000, 30000,
        [ctx](int result_code, int exit_code) {
            cp0_sudo_result_t result = static_cast<cp0_sudo_result_t>(result_code);
            if (!ctx->operation.complete()) return;
            if (result == CP0_SUDO_RESULT_SUCCESS) {
                ctx->rtc->state_.finish_commit(true);
                ctx->rtc->close_write_confirm();
                ctx->page->update_datetime_status();
                SetupPageAccess access(*ctx->page);
                access.set_view(SetupViewState::MAIN);
                access.build_main_view();
            } else {
                ctx->rtc->state_.finish_commit(false);
                if (result == CP0_SUDO_RESULT_CANCELLED)
                    ctx->rtc->close_write_confirm();
                else
                    ctx->rtc->show_result_error(result, exit_code, "Date/time save");
            }
        }, [&](int code, uint64_t id) { rc = code; request_id = id; });
    } catch (...) {
        rc = -1;
    }
    if (rc != 0) {
        async_operation_.abort(operation);
        show_status("Save failed", "Unable to start request", Modal::ERROR);
    }
    else async_operation_.activate(operation, request_id);
}

void RTC::show_result_error(cp0_sudo_result_t result, int exit_code, const char *operation)
{
    const char *reason = "Command failed";
    switch (classify_privileged_result(static_cast<int>(result))) {
    case PrivilegedResultKind::AUTH_FAILED: reason = "Authentication failed"; break;
    case PrivilegedResultKind::CANCELLED: reason = "Request cancelled"; break;
    case PrivilegedResultKind::TIMED_OUT: reason = "Request timed out"; break;
    case PrivilegedResultKind::EXEC_FAILED: reason = exit_code ? "Command returned an error" : "Unable to start command"; break;
    default: break;
    }
    char title[64];
    snprintf(title, sizeof(title), "%s failed", operation);
    show_status(title, reason, Modal::ERROR);
}

} // namespace setting
