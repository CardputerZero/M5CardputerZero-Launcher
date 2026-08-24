#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "settings_rtc_api.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <string>
#include <utility>

eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_osinfo_api;
eventpp::CallbackList<void(std::list<std::string>,
                           int,
                           int,
                           std::function<void(int, int)>,
                           std::function<void(int, uint64_t)>)>
    cp0_signal_sudo_argv_async;
eventpp::CallbackList<void(uint64_t, std::function<void(int)>)> cp0_signal_sudo_cancel;
eventpp::CallbackList<void(std::list<std::string>,
                           int,
                           int,
                           std::function<void(int, int)>,
                           std::function<void(int, uint64_t)>)>
    cp0_signal_system_admin_async;
eventpp::EventQueue<CP0_C_EVENT_t, void(const std::list<std::string>)> cp0_task_queue;
uint32_t lv_c_event[2 * CP0_C_EVENT_END] = {};

namespace {

std::function<void(std::list<std::string>, std::function<void(int, std::string)>)> osinfo_handler;
std::list<std::string> admin_arguments;
std::function<void(int, int)> admin_complete;
std::function<void(int, uint64_t)> admin_started;
uint64_t cancelled_request_id = 0;
int cancel_calls = 0;
std::string fallback_time = "23:59";
int fallback_ntp = -1;

} // namespace

std::string cp0_file_path(std::string file)
{
    return file;
}

extern "C" void cp0_time_str(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) return;
    std::snprintf(buffer, static_cast<std::size_t>(buffer_size), "%s", fallback_time.c_str());
}

extern "C" int cp0_time_ntp_get(void)
{
    return fallback_ntp;
}

int main()
{
    const auto osinfo_registration = cp0_signal_osinfo_api.append(
        [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
            if (osinfo_handler) osinfo_handler(std::move(arguments), std::move(callback));
        });
    const auto admin_registration = cp0_signal_system_admin_async.append(
        [](std::list<std::string> arguments,
           int,
           int,
           std::function<void(int, int)> complete,
           std::function<void(int, uint64_t)> started) {
            admin_arguments = std::move(arguments);
            admin_complete = std::move(complete);
            admin_started = std::move(started);
        });
    const auto cancel_registration = cp0_signal_sudo_cancel.append(
        [](uint64_t request_id, std::function<void(int)> callback) {
            ++cancel_calls;
            cancelled_request_id = request_id;
            if (callback) callback(0);
        });

    using namespace settings_rtc;

    osinfo_handler = [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"NtpGet"});
        callback(1, {});
    };
    NtpReadResult ntp_result;
    assert(read_ntp_async([&](NtpReadResult result) { ntp_result = std::move(result); }) == 0);
    assert(ntp_result.available && ntp_result.enabled && ntp_result.status == 1);

    osinfo_handler = [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"LocalTime"});
        callback(0, "2024,2,29,23,59,59");
    };
    TimeReadResult time_result;
    assert(read_local_time_async([&](TimeReadResult result) { time_result = std::move(result); }) == 0);
    assert(time_result.valid);
    assert((time_result.values == RtcValues{2024, 2, 29, 23, 59, 59}));

    osinfo_handler = [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"LocalTime"});
        callback(-1, "backend unavailable");
    };
    fallback_time = "23:59";
    TimeReadResult fallback_result;
    assert(read_local_time_async([&](TimeReadResult result) { fallback_result = std::move(result); }) == 0);
    assert(fallback_result.valid);
    assert(fallback_result.values[3] == 23 && fallback_result.values[4] == 59);

    osinfo_handler = [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
        if (arguments.front() == "NtpGet") callback(0, {});
        else callback(0, "2025,1,2,3,4,5");
    };
    RefreshResult refresh_result;
    assert(refresh_async([&](RefreshResult result) { refresh_result = std::move(result); }) == 0);
    assert(refresh_result.valid());
    assert(!refresh_result.ntp.enabled);
    assert((refresh_result.time.values == RtcValues{2025, 1, 2, 3, 4, 5}));

    int admin_calls = 0;
    PrivilegedResult set_result;
    assert(set_time_async(
               "2024-02-29 23:59:59",
               [&](PrivilegedResult result) {
                   ++admin_calls;
                   set_result = std::move(result);
               },
               [](int code, uint64_t request_id) {
                   assert(code == 0 && request_id == 42);
               }) == 0);
    assert((admin_arguments == std::list<std::string>{"TimeSet", "2024-02-29 23:59:59"}));
    admin_started(0, 42);
    admin_complete(0, 0);
    assert(admin_calls == 1 && set_result.succeeded());

    PrivilegedResult failed_result;
    assert(set_time_async(
               "2024-02-29 23:59:59",
               [&](PrivilegedResult result) { failed_result = std::move(result); }) == 0);
    admin_complete(0, 7);
    assert(!failed_result.succeeded());
    assert(failed_result.kind == PrivilegedResultKind::EXEC_FAILED);

    const std::size_t admin_call_count_before_invalid = admin_arguments.size();
    assert(set_time_async("2024-02-30 23:59:59", [](PrivilegedResult) {}) == kApiErrorInvalidArgument);
    assert(admin_arguments.size() == admin_call_count_before_invalid);
    assert(set_time_async("2024-02-29 23:59:59", {}) == kApiErrorInvalidArgument);
    assert(set_ntp_async(true, {}) == kApiErrorInvalidArgument);

    assert(set_ntp_async(true, [](PrivilegedResult) {}) == 0);
    assert((admin_arguments == std::list<std::string>{"NtpSet", "1"}));

    update_ntp_cache(0);
    bool ntp_enabled = true;
    settings_rtc_ntp_api(0, &ntp_enabled);
    assert(!ntp_enabled);
    settings_rtc_ntp_api(1, nullptr);
    assert((admin_arguments == std::list<std::string>{"NtpSet", "1"}));
    admin_complete(1, -1);
    settings_rtc_ntp_api(0, &ntp_enabled);
    assert(!ntp_enabled);

    osinfo_handler = [](std::list<std::string> arguments, std::function<void(int, std::string)> callback) {
        assert(arguments == std::list<std::string>{"NtpGet"});
        callback(1, {});
    };
    settings_rtc_ntp_api(1, nullptr);
    admin_complete(0, 0);
    settings_rtc_ntp_api(0, &ntp_enabled);
    assert(ntp_enabled);

    auto &workflow = session();
    workflow.reset();
    update_ntp_cache(0);
    assert(workflow.edit_field(RtcField::SECOND, 1));
    const std::list<std::string> previous_admin_arguments = admin_arguments;
    settings_rtc_ntp_api(1, nullptr);
    assert(admin_arguments == previous_admin_arguments);
    workflow.discard_edits();

    assert(cancel_request(42) == 0);
    assert(cancel_calls == 1 && cancelled_request_id == 42);
    assert(cancel_request(0) == kApiErrorInvalidArgument);

    assert(classify_privileged_result(0, 0) == PrivilegedResultKind::SUCCESS);
    assert(classify_privileged_result(0, 7) == PrivilegedResultKind::EXEC_FAILED);

    cp0_signal_sudo_cancel.remove(cancel_registration);
    cp0_signal_system_admin_async.remove(admin_registration);
    cp0_signal_osinfo_api.remove(osinfo_registration);
    return 0;
}
