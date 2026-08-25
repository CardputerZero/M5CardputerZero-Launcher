#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "lvgl_components.hpp"
#include "settings_audio_api.hpp"

class LvSettingVolumePage3 : public LvSettingValuePage3Base {
public:
    LvSettingVolumePage3() = default;

    LvSettingVolumePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize_page(parent, {});
    }

    LvSettingVolumePage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize_page(parent, std::move(back_callback));
    }

    ~LvSettingVolumePage3() override
    {
        destroying_ = true;
        ++generation_;
        LeaveSelfPage = nullptr;
        cancel_preview_timer();
        cancel_async_tasks();
        back_callback_ = nullptr;
    }

    static int selection_for_volume(int value)
    {
        return settings_audio::volume_index(value);
    }

    static int volume_for_selection(int index)
    {
        return settings_audio::volume_percent(index);
    }

    bool ready() const
    {
        return ready_;
    }

    bool request_pending() const
    {
        return active_request_.has_value();
    }

    int backend_volume() const
    {
        return backend_volume_;
    }

    int original_volume() const
    {
        return original_volume_;
    }

    const std::string &last_error() const
    {
        return last_error_;
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }

    SettingApiResult activate_selected() override
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

    void create_ui(lv_obj_t *parent) override
    {
        LvSettingValuePage3Base::create_ui(parent);
        if (!ComponensObj) return;

        OnEvent(static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS),
                std::function<void(lv_event_t *)>([this](lv_event_t *event) {
                    observe_key_event(event);
                }),
                nullptr);
    }

private:
    enum class RequestKind {
        Read,
        PreviewWrite,
        CommitWrite,
        RestoreWrite,
        PreviewSound,
        CommitSound,
    };

    struct ActiveRequest {
        std::uint64_t id = 0;
        std::uint64_t generation = 0;
        RequestKind kind = RequestKind::Read;
        int value = 0;
    };

    static constexpr std::chrono::milliseconds kRequestTimeout{4000};
    static constexpr uint32_t kPreviewDelayMs = 180;
    static constexpr int kVolumeOptionCount = settings_audio::kMaxVolume / 10 + 1;

    void initialize_page(lv_obj_t *parent, std::function<void()> back_callback)
    {
        back_callback_ = std::move(back_callback);
        LeaveSelfPage = [this] { request_back(); };
        initialize(parent);
        if (ComponensObj) start_read_request();
    }

    void select_volume(int index)
    {
        select(index);
    }

    void observe_key_event(lv_event_t *event)
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
        if (next_index < 0 || next_index >= kVolumeOptionCount) {
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

    void request_back()
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

    void finish_back()
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

    bool request_is_current(const ActiveRequest &request) const
    {
        return !destroying_ && active_request_ && active_request_->id == request.id &&
               active_request_->generation == request.generation;
    }

    void start_read_request()
    {
        ready_ = false;
        start_volume_request(RequestKind::Read, 0);
    }

    void start_volume_request(RequestKind kind, int value)
    {
        if (destroying_ || active_request_) return;

        const ActiveRequest request{++next_request_id_, generation_, kind, value};
        active_request_ = request;

        const bool scheduled = run_async_task<settings_audio::VolumeResponse>(
            {[
                 kind,
                 value
             ] {
                 return kind == RequestKind::Read ? settings_audio::read_volume()
                                                   : settings_audio::write_volume(value);
             },
             {},
             {},
             {},
             [this, request](DComponens::LvglComponensBase::AsyncTaskContext &, const settings_audio::VolumeResponse &result) {
                 handle_volume_result(request, result);
             },
             [this, request](DComponens::LvglComponensBase::AsyncTaskContext &, std::exception_ptr) {
                 handle_volume_failure(request, settings_audio::kErrorInvoker,
                                       "audio request failed");
             },
             [this, request](DComponens::LvglComponensBase::AsyncTaskContext &) {
                 handle_volume_failure(request, settings_audio::kErrorTimeout,
                                       "audio request timed out");
             },
             [this, request](DComponens::LvglComponensBase::AsyncTaskContext &) {
                 handle_volume_failure(request, settings_audio::kErrorInvoker,
                                       "audio request could not be scheduled");
             }},
            {kRequestTimeout});

        if (!scheduled && request_is_current(request))
            handle_volume_failure(request, settings_audio::kErrorInvoker,
                                  "audio request could not be scheduled");
    }

    void start_command_request(RequestKind kind)
    {
        if (destroying_ || active_request_) return;

        const ActiveRequest request{++next_request_id_, generation_, kind, 0};
        active_request_ = request;
        const bool scheduled = run_async_task<settings_audio::Response>(
            {[] { return settings_audio::play_system_sound(); },
             {},
             {},
             {},
             [this, request](DComponens::LvglComponensBase::AsyncTaskContext &, const settings_audio::Response &result) {
                 handle_command_result(request, result);
             },
             [this, request](DComponens::LvglComponensBase::AsyncTaskContext &, std::exception_ptr) {
                 handle_command_result(request,
                                       {settings_audio::kErrorInvoker, "audio sound request failed"});
             },
             [this, request](DComponens::LvglComponensBase::AsyncTaskContext &) {
                 handle_command_result(request,
                                       {settings_audio::kErrorTimeout, "audio sound request timed out"});
             },
             [this, request](DComponens::LvglComponensBase::AsyncTaskContext &) {
                 handle_command_result(request,
                                       {settings_audio::kErrorInvoker,
                                        "audio sound request could not be scheduled"});
             }},
            {kRequestTimeout});

        if (!scheduled && request_is_current(request))
            handle_command_result(request,
                                  {settings_audio::kErrorInvoker,
                                   "audio sound request could not be scheduled"});
    }

    void handle_volume_failure(const ActiveRequest &request, int code, std::string message)
    {
        handle_volume_result(request, {code, -1, std::move(message)});
    }

    void handle_volume_result(const ActiveRequest &request,
                              const settings_audio::VolumeResponse &result)
    {
        if (!request_is_current(request)) return;
        active_request_.reset();

        const bool valid = result.code == 0 && settings_audio::volume_value_valid(result.value);
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
                original_volume_ = settings_audio::kMaxVolume;
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

    void handle_preview_success(const ActiveRequest &request)
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
            if (!selection_still_matches || current != request.value) {
                start_volume_request(RequestKind::CommitWrite, current);
            } else {
                accept_commit_without_write();
            }
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

    void handle_write_failure()
    {
        cancel_preview_timer();
        preview_sound_pending_ = false;
        commit_requested_      = false;
        commit_leave_requested_ = false;
        select_volume(selection_for_volume(original_volume_));
        start_restore_if_needed(true);
        return;
    }

    void start_restore_if_needed(bool force_write = false)
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

    void accept_commit_without_write()
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

    void start_commit_sound()
    {
        cancel_preview_timer();
        preview_sound_pending_ = false;
        locked_selection_      = selection_for_volume(backend_volume_);
        if (!active_request_)
            start_command_request(RequestKind::CommitSound);
    }

    void schedule_preview_sound()
    {
        if (destroying_ || back_requested_ || commit_requested_ || commit_leave_requested_ ||
            rollback_pending_)
            return;
        cancel_preview_timer();
        preview_sound_pending_ = false;
        preview_timer_         = lv_timer_create(preview_timer_cb, kPreviewDelayMs, this);
        if (!preview_timer_) {
            preview_sound_pending_ = true;
            if (!active_request_) start_preview_sound();
            return;
        }
        lv_timer_set_repeat_count(preview_timer_, 1);
    }

    void start_preview_sound()
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

    static void preview_timer_cb(lv_timer_t *timer) noexcept
    {
        auto *self = timer ? static_cast<LvSettingVolumePage3 *>(lv_timer_get_user_data(timer)) : nullptr;
        if (!self || self->destroying_ || timer != self->preview_timer_) return;
        self->preview_timer_ = nullptr;
        self->start_preview_sound();
    }

    void cancel_preview_timer()
    {
        if (!preview_timer_) return;
        lv_timer_delete(preview_timer_);
        preview_timer_ = nullptr;
    }

    void handle_command_result(const ActiveRequest &request,
                               const settings_audio::Response &result)
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

    void pump_preview()
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

    void pump_commit()
    {
        if (!commit_requested_ || active_request_ || destroying_) return;
        const int requested = volume_for_selection(selected_index);
        if (requested == backend_volume_)
            accept_commit_without_write();
        else
            start_volume_request(RequestKind::CommitWrite, requested);
    }

    std::function<void()> back_callback_;
    std::optional<ActiveRequest> active_request_;
    std::uint64_t next_request_id_ = 0;
    std::uint64_t generation_ = 1;
    bool destroying_ = false;
    bool ready_ = false;
    bool preview_touched_ = false;
    bool commit_requested_ = false;
    bool commit_leave_requested_ = false;
    bool back_requested_ = false;
    bool rollback_pending_ = false;
    bool preview_sound_pending_ = false;
    int original_volume_ = settings_audio::kMaxVolume;
    int backend_volume_ = settings_audio::kMaxVolume;
    int locked_selection_ = 0;
    int last_preview_requested_ = settings_audio::kMaxVolume;
    int last_preview_actual_ = settings_audio::kMaxVolume;
    std::string last_error_;
    lv_timer_t *preview_timer_ = nullptr;
};
