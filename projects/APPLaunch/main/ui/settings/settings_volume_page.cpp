#include "settings_volume_page.hpp"

#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <list>
#include <memory>
#include <mutex>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr std::chrono::milliseconds kRequestTimeout{4000};

enum class TimerInterval : uint32_t {
    PreviewDelayMs = 180,
};

class settings_audio_com {
public:
    enum class ErrorCode : int {
        Invalid = -1,
        Timeout = -2,
        Invoker = -3,
    };
    using ResponseCallback = std::function<void(int, std::string)>;

private:
    struct WaitState {
        std::mutex mutex;
        std::condition_variable condition;
        bool completed = false;
        LvSettingVolumePage3::Response response;
    };

    static bool parse_integer(std::string_view data, int minimum, int maximum, int &value)
    {
        if (data.empty()) return false;
        int parsed = 0;
        const auto result = std::from_chars(data.data(), data.data() + data.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != data.data() + data.size() ||
            parsed < minimum || parsed > maximum)
            return false;
        value = parsed;
        return true;
    }

    static bool parse_boolean(std::string_view data, bool &enabled)
    {
        if (data == "1" || data == "on" || data == "true" || data == "enable" || data == "enabled") {
            enabled = true;
            return true;
        }
        if (data == "0" || data == "off" || data == "false" || data == "disable" || data == "disabled") {
            enabled = false;
            return true;
        }
        return false;
    }

    static LvSettingVolumePage3::Response invoke_default(
        const std::list<std::string> &args, std::chrono::milliseconds timeout)
    {
        return invoke(args,
                      [](std::list<std::string> command, ResponseCallback callback) {
                          cp0_signal_audio_api(std::move(command), std::move(callback));
                      },
                      timeout);
    }

    static LvSettingVolumePage3::Response invoke(
        const std::list<std::string> &args,
        const std::function<void(std::list<std::string>, ResponseCallback)> &invoker,
        std::chrono::milliseconds timeout)
    {
        if (!invoker)
            return {static_cast<int>(ErrorCode::Invoker), "audio api invoker unavailable"};
        auto state = std::make_shared<WaitState>();
        try {
            invoker(args, [state](int code, std::string data) noexcept {
                try {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->completed) return;
                    state->response.code = code;
                    state->response.data = std::move(data);
                    state->completed = true;
                    state->condition.notify_one();
                } catch (...) {
                }
            });
        } catch (...) {
            return {static_cast<int>(ErrorCode::Invoker), "audio api invocation failed"};
        }
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->condition.wait_for(lock, timeout, [state] { return state->completed; }))
            return {static_cast<int>(ErrorCode::Timeout), "audio api timeout"};
        return state->response;
    }

    static LvSettingVolumePage3::VolumeResponse decode_volume(const LvSettingVolumePage3::Response &response)
    {
        LvSettingVolumePage3::VolumeResponse result{response.code, -1, response.data};
        if (response.code != 0 || !parse_integer(response.data,
                                                 LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MinVolume),
                                                 LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MaxVolume),
                                                 result.value)) {
            if (response.code == 0) result.code = static_cast<int>(ErrorCode::Invalid);
            result.value = -1;
        }
        return result;
    }

public:
    static bool volume_value_valid(int value)
    {
        return value >= LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MinVolume) &&
               value <= LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MaxVolume);
    }

    static int volume_index(int value)
    {
        const int clamped = std::clamp(value,
                                       LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MinVolume),
                                       LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MaxVolume));
        const int rounded = std::min(
            LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MaxVolume), ((clamped + 5) / 10) * 10);
        return (LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MaxVolume) - rounded) / 10;
    }

    static int volume_percent(int index)
    {
        const int clamped_index = std::clamp(index, 0, 10);
        return LvSettingVolumePage3::metric(LvSettingVolumePage3::LayoutMetric::MaxVolume) - clamped_index * 10;
    }

    static LvSettingVolumePage3::VolumeResponse read_volume()
    {
        return decode_volume(invoke_default({"VolumeRead"}, std::chrono::milliseconds(3000)));
    }

    static LvSettingVolumePage3::VolumeResponse write_volume(int value)
    {
        if (!volume_value_valid(value))
            return {static_cast<int>(ErrorCode::Invalid), -1, "volume out of range"};
        return decode_volume(invoke_default({"VolumeWrite", std::to_string(value)}, std::chrono::milliseconds(3000)));
    }

    static LvSettingVolumePage3::Response play_system_sound(int index)
    {
        if (index < 0 || index > 2)
            return {static_cast<int>(ErrorCode::Invalid), "system sound index out of range"};
        return invoke_default({"SystemSoundPlay", std::to_string(index)}, std::chrono::milliseconds(3000));
    }
};

} // namespace

LvSettingVolumePage3::LvSettingVolumePage3() = default;

LvSettingVolumePage3::LvSettingVolumePage3(lv_obj_t *parent, const NodeIter &parent_node)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize_page(parent, {});
}

LvSettingVolumePage3::LvSettingVolumePage3(lv_obj_t *parent,
                                           const NodeIter &parent_node,
                                           std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize_page(parent, std::move(back_callback));
}

LvSettingVolumePage3::~LvSettingVolumePage3()
{
    destroying_ = true;
    ++generation_;
    LeaveSelfPage = nullptr;
    cancel_preview_timer();
    cancel_async_tasks();
    back_callback_ = nullptr;
}

int LvSettingVolumePage3::selection_for_volume(int value)
{
    return settings_audio_com::volume_index(value);
}

int LvSettingVolumePage3::volume_for_selection(int index)
{
    return settings_audio_com::volume_percent(index);
}

bool LvSettingVolumePage3::ready() const
{
    return ready_;
}

bool LvSettingVolumePage3::request_pending() const
{
    return active_request_.has_value();
}

int LvSettingVolumePage3::backend_volume() const
{
    return backend_volume_;
}

int LvSettingVolumePage3::original_volume() const
{
    return original_volume_;
}

const std::string &LvSettingVolumePage3::last_error() const
{
    return last_error_;
}

int LvSettingVolumePage3::initial_selection() const
{
    return 0;
}

SettingApiResult LvSettingVolumePage3::activate_selected()
{
    if (!ready_ || destroying_ || back_requested_ || commit_requested_ ||
        commit_leave_requested_ || rollback_pending_)
        return SettingApiResult::Pending;
    if (active_request_ && active_request_->kind == RequestKind::RestoreWrite)
        return SettingApiResult::Pending;

    cancel_preview_timer();
    preview_sound_pending_ = false;
    commit_requested_      = true;
    commit_leave_requested_ = true;
    locked_selection_       = selected_index;

    if (active_request_) return SettingApiResult::Pending;
    const int requested = volume_for_selection(selected_index);
    if (requested == backend_volume_)
        accept_commit_without_write();
    else
        start_volume_request(RequestKind::CommitWrite, requested);
    return SettingApiResult::Pending;
}

void LvSettingVolumePage3::create_ui(lv_obj_t *parent)
{
    LvSettingValuePage3Base::create_ui(parent);
    if (!ComponensObj) return;

    OnEvent(static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS),
            std::function<void(lv_event_t *)>([this](lv_event_t *event) {
                observe_key_event(event);
            }),
            nullptr);
}

void LvSettingVolumePage3::initialize_page(lv_obj_t *parent, std::function<void()> back_callback)
{
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
    if (ComponensObj) start_read_request();
}

void LvSettingVolumePage3::select_volume(int index)
{
    select(index);
}

void LvSettingVolumePage3::observe_key_event(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        request_back();
        lv_event_stop_processing(event);
        return;
    }
    if (key != LV_KEY_UP && key != LV_KEY_DOWN) return;

    if (!ready_ || destroying_ || commit_requested_ || commit_leave_requested_ ||
        back_requested_ || rollback_pending_) {
        select_volume(locked_selection_);
        lv_event_stop_processing(event);
        return;
    }

    const int delta = key == LV_KEY_UP ? -1 : 1;
    const int next_index = selected_index + delta;
    if (next_index < 0 || next_index >= metric(LayoutMetric::VolumeOptionCount)) {
        lv_event_stop_processing(event);
        return;
    }
    cancel_preview_timer();
    preview_sound_pending_ = false;
    preview_touched_       = true;
    select_volume(next_index);

    if (active_request_) {
        if (active_request_->kind == RequestKind::PreviewSound) preview_sound_pending_ = true;
        lv_event_stop_processing(event);
        return;
    }

    const int requested = volume_for_selection(selected_index);
    if (requested == backend_volume_)
        schedule_preview_sound();
    else
        start_volume_request(RequestKind::PreviewWrite, requested);
    lv_event_stop_processing(event);
}

void LvSettingVolumePage3::request_back()
{
    if (destroying_ || back_requested_) return;

    cancel_preview_timer();
    preview_sound_pending_ = false;
    commit_requested_      = false;
    commit_leave_requested_ = false;
    back_requested_        = true;
    locked_selection_      = selection_for_volume(original_volume_);

    if (!ready_ || !active_request_) {
        if (active_request_ &&
            (active_request_->kind == RequestKind::Read ||
             active_request_->kind == RequestKind::PreviewSound ||
             active_request_->kind == RequestKind::CommitSound))
            active_request_.reset();
        if (!active_request_) {
            if (backend_volume_ != original_volume_)
                start_volume_request(RequestKind::RestoreWrite, original_volume_);
            else
                finish_back();
        }
        return;
    }

    if (active_request_->kind == RequestKind::PreviewSound) {
        active_request_.reset();
        if (backend_volume_ != original_volume_)
            start_restore_if_needed();
        else
            finish_back();
        return;
    }
    if (active_request_->kind == RequestKind::CommitSound) {
        active_request_.reset();
        finish_back();
    }
}

void LvSettingVolumePage3::finish_back()
{
    if (destroying_ || !back_requested_ || active_request_) return;

    back_requested_ = false;
    destroying_ = true;
    ++generation_;
    advance_async_generation();
    LeaveSelfPage = nullptr;
    auto callback = std::move(back_callback_);
    back_callback_ = nullptr;
    if (callback) callback();
}

bool LvSettingVolumePage3::request_is_current(const ActiveRequest &request) const
{
    return !destroying_ && active_request_ && active_request_->id == request.id &&
           active_request_->generation == request.generation;
}

void LvSettingVolumePage3::start_read_request()
{
    ready_ = false;
    start_volume_request(RequestKind::Read, 0);
}

void LvSettingVolumePage3::start_volume_request(RequestKind kind, int value)
{
    if (destroying_ || active_request_) return;

    const ActiveRequest request{++next_request_id_, generation_, kind, value};
    active_request_ = request;

    const bool scheduled = run_async_task<VolumeResponse>(
        {[
             kind,
             value
         ] {
             return kind == RequestKind::Read ? settings_audio_com::read_volume()
                                               : settings_audio_com::write_volume(value);
         },
         {},
         {},
         {},
         [this, request](DComponens::LvglComponensBase::AsyncTaskContext &, const VolumeResponse &result) {
             handle_volume_result(request, result);
         },
         [this, request](DComponens::LvglComponensBase::AsyncTaskContext &, std::exception_ptr) {
             handle_volume_failure(request, static_cast<int>(settings_audio_com::ErrorCode::Invoker),
                                   "audio request failed");
         },
         [this, request](DComponens::LvglComponensBase::AsyncTaskContext &) {
             handle_volume_failure(request, static_cast<int>(settings_audio_com::ErrorCode::Timeout),
                                   "audio request timed out");
         },
         [this, request](DComponens::LvglComponensBase::AsyncTaskContext &) {
             handle_volume_failure(request, static_cast<int>(settings_audio_com::ErrorCode::Invoker),
                                   "audio request could not be scheduled");
         }},
        {kRequestTimeout});

    if (!scheduled && request_is_current(request))
        handle_volume_failure(request, static_cast<int>(settings_audio_com::ErrorCode::Invoker),
                              "audio request could not be scheduled");
}

void LvSettingVolumePage3::start_command_request(RequestKind kind)
{
    if (destroying_ || active_request_) return;

    const ActiveRequest request{++next_request_id_, generation_, kind, 0};
    active_request_ = request;
    const bool scheduled = run_async_task<Response>(
        {[] {
             return settings_audio_com::play_system_sound(metric(LayoutMetric::SystemSoundSwitchIndex));
         },
         {},
         {},
         {},
         [this, request](DComponens::LvglComponensBase::AsyncTaskContext &, const Response &result) {
             handle_command_result(request, result);
         },
         [this, request](DComponens::LvglComponensBase::AsyncTaskContext &, std::exception_ptr) {
             handle_command_result(request,
                                   {static_cast<int>(settings_audio_com::ErrorCode::Invoker),
                                    "audio sound request failed"});
         },
         [this, request](DComponens::LvglComponensBase::AsyncTaskContext &) {
             handle_command_result(request,
                                   {static_cast<int>(settings_audio_com::ErrorCode::Timeout),
                                    "audio sound request timed out"});
         },
         [this, request](DComponens::LvglComponensBase::AsyncTaskContext &) {
             handle_command_result(request,
                                   {static_cast<int>(settings_audio_com::ErrorCode::Invoker),
                                    "audio sound request could not be scheduled"});
         }},
        {kRequestTimeout});

    if (!scheduled && request_is_current(request))
        handle_command_result(request,
                              {static_cast<int>(settings_audio_com::ErrorCode::Invoker),
                               "audio sound request could not be scheduled"});
}

void LvSettingVolumePage3::handle_volume_failure(const ActiveRequest &request, int code, std::string message)
{
    handle_volume_result(request, {code, -1, std::move(message)});
}

void LvSettingVolumePage3::handle_volume_result(const ActiveRequest &request,
                                                const VolumeResponse &result)
{
    if (!request_is_current(request)) return;
    active_request_.reset();

    const bool valid = result.code == 0 && settings_audio_com::volume_value_valid(result.value);
    if (request.kind == RequestKind::Read) {
        if (valid) {
            original_volume_       = result.value;
            backend_volume_        = result.value;
            last_preview_requested_ = result.value;
            last_preview_actual_    = result.value;
            ready_                  = true;
            last_error_.clear();
            select_volume(selection_for_volume(result.value));
        } else {
            original_volume_ = metric(LayoutMetric::MaxVolume);
            backend_volume_  = original_volume_;
            ready_            = true;
            last_error_       = result.data.empty() ? "unable to read volume" : result.data;
            select_volume(0);
        }
        if (back_requested_) finish_back();
        return;
    }

    if (request.kind == RequestKind::RestoreWrite) {
        rollback_pending_ = false;
        commit_requested_ = false;
        commit_leave_requested_ = false;
        if (valid)
            backend_volume_ = result.value;
        else
            last_error_ = result.data.empty() ? "unable to restore volume" : result.data;
        select_volume(selection_for_volume(original_volume_));
        preview_touched_        = false;
        last_preview_requested_ = backend_volume_;
        last_preview_actual_    = backend_volume_;
        if (back_requested_)
            finish_back();
        return;
    }

    if (!valid) {
        last_error_ = result.data.empty() ? "volume write rejected" : result.data;
        handle_write_failure();
        return;
    }

    backend_volume_ = result.value;
    last_error_.clear();
    if (request.kind == RequestKind::PreviewWrite) {
        handle_preview_success(request);
        return;
    }

    if (back_requested_) {
        start_restore_if_needed();
        return;
    }

    original_volume_        = result.value;
    preview_touched_        = false;
    last_preview_requested_ = result.value;
    last_preview_actual_    = result.value;
    select_volume(selection_for_volume(result.value));
    commit_requested_ = false;
    start_commit_sound();
}

void LvSettingVolumePage3::handle_preview_success(const ActiveRequest &request)
{
    const int selected_volume_before_refresh = volume_for_selection(selected_index);
    const bool selection_still_matches = selected_volume_before_refresh == request.value;
    if (selection_still_matches) select_volume(selection_for_volume(backend_volume_));

    if (back_requested_) {
        start_restore_if_needed();
        return;
    }

    if (commit_requested_) {
        const int current = volume_for_selection(selected_index);
        if (!selection_still_matches || current != request.value)
            start_volume_request(RequestKind::CommitWrite, current);
        else
            accept_commit_without_write();
        return;
    }

    const int current = volume_for_selection(selected_index);
    if (!selection_still_matches || current != request.value) {
        if (current != backend_volume_)
            start_volume_request(RequestKind::PreviewWrite, current);
        else
            schedule_preview_sound();
        return;
    }
    schedule_preview_sound();
}

void LvSettingVolumePage3::handle_write_failure()
{
    cancel_preview_timer();
    preview_sound_pending_ = false;
    commit_requested_      = false;
    commit_leave_requested_ = false;
    select_volume(selection_for_volume(original_volume_));
    start_restore_if_needed(true);
}

void LvSettingVolumePage3::start_restore_if_needed(bool force_write)
{
    cancel_preview_timer();
    preview_sound_pending_ = false;
    locked_selection_      = selection_for_volume(original_volume_);
    rollback_pending_      = true;
    if (active_request_) return;
    if (!force_write && backend_volume_ == original_volume_) {
        rollback_pending_ = false;
        select_volume(locked_selection_);
        if (back_requested_) finish_back();
        return;
    }
    start_volume_request(RequestKind::RestoreWrite, original_volume_);
}

void LvSettingVolumePage3::accept_commit_without_write()
{
    if (active_request_) return;
    original_volume_         = backend_volume_;
    preview_touched_         = false;
    last_preview_requested_  = backend_volume_;
    last_preview_actual_     = backend_volume_;
    commit_requested_        = false;
    select_volume(selection_for_volume(backend_volume_));
    start_commit_sound();
}

void LvSettingVolumePage3::start_commit_sound()
{
    cancel_preview_timer();
    preview_sound_pending_ = false;
    locked_selection_      = selection_for_volume(backend_volume_);
    if (!active_request_)
        start_command_request(RequestKind::CommitSound);
}

void LvSettingVolumePage3::schedule_preview_sound()
{
    if (destroying_ || back_requested_ || commit_requested_ || commit_leave_requested_ ||
        rollback_pending_)
        return;
    cancel_preview_timer();
    preview_sound_pending_ = false;
    preview_timer_ = lv_timer_create(
        preview_timer_cb, static_cast<uint32_t>(TimerInterval::PreviewDelayMs), this);
    if (!preview_timer_) {
        preview_sound_pending_ = true;
        if (!active_request_) start_preview_sound();
        return;
    }
    lv_timer_set_repeat_count(preview_timer_, 1);
}

void LvSettingVolumePage3::start_preview_sound()
{
    if (destroying_ || back_requested_ || commit_requested_ || commit_leave_requested_ ||
        rollback_pending_)
        return;
    preview_sound_pending_ = false;
    if (active_request_) {
        preview_sound_pending_ = true;
        return;
    }
    start_command_request(RequestKind::PreviewSound);
}

void LvSettingVolumePage3::preview_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = timer ? static_cast<LvSettingVolumePage3 *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!self || self->destroying_ || timer != self->preview_timer_) return;
    self->preview_timer_ = nullptr;
    self->start_preview_sound();
}

void LvSettingVolumePage3::cancel_preview_timer()
{
    if (!preview_timer_) return;
    lv_timer_delete(preview_timer_);
    preview_timer_ = nullptr;
}

void LvSettingVolumePage3::handle_command_result(const ActiveRequest &request,
                                                 const Response &result)
{
    if (!request_is_current(request)) return;
    active_request_.reset();
    if (result.code != 0 && last_error_.empty()) last_error_ = result.data;

    if (request.kind == RequestKind::CommitSound) {
        commit_leave_requested_ = false;
        commit_requested_        = false;
        back_requested_          = true;
        original_volume_         = backend_volume_;
        finish_back();
        return;
    }

    if (back_requested_) {
        if (backend_volume_ != original_volume_)
            start_restore_if_needed();
        else
            finish_back();
        return;
    }

    if (commit_requested_) {
        pump_commit();
        return;
    }

    if (preview_sound_pending_) {
        preview_sound_pending_ = false;
        pump_preview();
    }
}

void LvSettingVolumePage3::pump_preview()
{
    if (destroying_ || back_requested_ || commit_requested_ || commit_leave_requested_ ||
        rollback_pending_)
        return;
    if (active_request_) {
        preview_sound_pending_ = true;
        return;
    }

    const int requested = volume_for_selection(selected_index);
    if (requested == backend_volume_)
        schedule_preview_sound();
    else
        start_volume_request(RequestKind::PreviewWrite, requested);
}

void LvSettingVolumePage3::pump_commit()
{
    if (!commit_requested_ || active_request_ || destroying_) return;
    const int requested = volume_for_selection(selected_index);
    if (requested == backend_volume_)
        accept_commit_without_write();
    else
        start_volume_request(RequestKind::CommitWrite, requested);
}
