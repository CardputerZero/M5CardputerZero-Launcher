#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "lvgl_components.hpp"
#include "../model/setup_value_policy.hpp"

class LvSettingVolumePage3 : public LvSettingValuePage3Base {
public:
    enum class LayoutMetric : int {
        MinVolume = setup_values::volume_metric(setup_values::VolumeMetric::MinPercent),
        MaxVolume = setup_values::volume_metric(setup_values::VolumeMetric::MaxPercent),
        VolumeOptionCount = setup_values::volume_metric(setup_values::VolumeMetric::OptionCount),
        SystemSoundSwitchIndex = 1,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingVolumePage3();

    LvSettingVolumePage3(lv_obj_t *parent, const NodeIter &parent_node);

    LvSettingVolumePage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback);

    ~LvSettingVolumePage3() override;

    static int selection_for_volume(int value);
    static int volume_for_selection(int index);

    struct Response {
        int code = -3;
        std::string data;
    };

    struct VolumeResponse {
        int code = -3;
        int value = -1;
        std::string data;
    };

    bool ready() const;
    bool request_pending() const;
    int backend_volume() const;
    int original_volume() const;
    const std::string &last_error() const;

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;
    void create_ui(lv_obj_t *parent) override;

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

    void initialize_page(lv_obj_t *parent, std::function<void()> back_callback);
    void select_volume(int index);
    void observe_key_event(lv_event_t *event);
    void request_back();
    void finish_back();
    bool request_is_current(const ActiveRequest &request) const;
    void start_read_request();
    void start_volume_request(RequestKind kind, int value);
    void start_command_request(RequestKind kind);
    void handle_volume_failure(const ActiveRequest &request, int code, std::string message);
    void handle_volume_result(const ActiveRequest &request,
                              const VolumeResponse &result);
    void handle_preview_success(const ActiveRequest &request);
    void handle_write_failure();
    void start_restore_if_needed(bool force_write = false);
    void accept_commit_without_write();
    void start_commit_sound();
    void schedule_preview_sound();
    void start_preview_sound();
    static void preview_timer_cb(lv_timer_t *timer) noexcept;
    void cancel_preview_timer();
    void handle_command_result(const ActiveRequest &request,
                               const Response &result);
    void pump_preview();
    void pump_commit();

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
    int original_volume_ = metric(LayoutMetric::MaxVolume);
    int backend_volume_ = metric(LayoutMetric::MaxVolume);
    int locked_selection_ = 0;
    int last_preview_requested_ = metric(LayoutMetric::MaxVolume);
    int last_preview_actual_ = metric(LayoutMetric::MaxVolume);
    std::string last_error_;
    lv_timer_t *preview_timer_ = nullptr;
};
