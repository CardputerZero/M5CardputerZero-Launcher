#include "settings_camera_resolution_page.hpp"

#include <exception>
#include <utility>

LvSettingResolutionPage3::LvSettingResolutionPage3() = default;

LvSettingResolutionPage3::LvSettingResolutionPage3(lv_obj_t *parent, const NodeIter &parent_node)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize(parent);
    create_status_label();
    begin_load();
}

LvSettingResolutionPage3::LvSettingResolutionPage3(lv_obj_t *parent,
                                                   const NodeIter &parent_node,
                                                   std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, std::move(back_callback))
{
    initialize(parent);
    create_status_label();
    begin_load();
}

LvSettingResolutionPage3::~LvSettingResolutionPage3()
{
    page_alive_ = false;
    ++generation_;
    pending_ = false;
    cancel_async_tasks();
    status_label_ = nullptr;
}

int LvSettingResolutionPage3::initial_selection() const
{
    return 0;
}

SettingApiResult LvSettingResolutionPage3::activate_selected()
{
    if (pending_) return SettingApiResult::Pending;
    if (!loaded_) {
        begin_load();
        return SettingApiResult::Pending;
    }

    return begin_write() ? SettingApiResult::Pending : SettingApiResult::Failure;
}

camera_resolution_settings::ConfigInvoker LvSettingResolutionPage3::config_invoker()
{
    return [](camera_resolution_settings::ConfigArguments arguments,
              camera_resolution_settings::ConfigCallback callback) {
        cp0_signal_config_api(std::move(arguments), std::move(callback));
    };
}

bool LvSettingResolutionPage3::request_is_current(std::uint64_t generation) const
{
    return page_alive_ && generation_ == generation;
}

void LvSettingResolutionPage3::restore_focus()
{
    if (!ComponensObj) return;
    if (lv_obj_get_group(ComponensObj)) lv_group_focus_obj(ComponensObj);
}

void LvSettingResolutionPage3::create_status_label()
{
    if (!ComponensObj) return;

    status_label_ = lv_label_create(ComponensObj);
    if (!status_label_) return;
    lv_obj_set_width(status_label_, 80);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(
        status_label_, cp0_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFFCC66), LV_PART_MAIN);
    lv_obj_set_pos(status_label_, 4, 4);
    lv_label_set_text(status_label_, "");
}

void LvSettingResolutionPage3::set_status(const char *text, bool error)
{
    if (!status_label_) return;
    lv_label_set_text(status_label_, text ? text : "");
    lv_obj_set_style_text_color(status_label_, lv_color_hex(error ? 0xFF6666 : 0x66CC88), LV_PART_MAIN);
}

void LvSettingResolutionPage3::finish_load(const camera_resolution_settings::ReadResult &result)
{
    if (!result.usable()) {
        loaded_ = false;
        saved_resolution_ = camera_resolution_settings::kDefaultResolution;
        saved_index_ = 0;
        select(saved_index_);
        restore_focus();
        set_status("Camera config read failed", true);
        return;
    }

    const int index = camera_resolution_settings::index_for_resolution(result.resolution);
    if (index < 0) {
        loaded_ = false;
        saved_resolution_ = camera_resolution_settings::kDefaultResolution;
        saved_index_ = 0;
        select(saved_index_);
        restore_focus();
        set_status("Camera resolution is unsupported", true);
        return;
    }

    loaded_ = true;
    saved_resolution_ = result.resolution;
    saved_index_ = index;
    select(saved_index_);
    if (result.defaulted())
        set_status("Invalid camera config; using 1280x720", true);
    else
        set_status("", false);
}

void LvSettingResolutionPage3::finish_load_failure()
{
    loaded_ = false;
    saved_resolution_ = camera_resolution_settings::kDefaultResolution;
    saved_index_ = 0;
    select(saved_index_);
    restore_focus();
    set_status("Camera config read failed", true);
}

void LvSettingResolutionPage3::begin_load()
{
    if (!ComponensObj || pending_) return;

    pending_ = true;
    loaded_ = false;
    const std::uint64_t request_generation = ++generation_;
    set_status("Loading camera config", false);

    DComponens::LvglComponensBase::AsyncTaskCallbacks<camera_resolution_settings::ReadResult> callbacks;
    const camera_resolution_settings::ConfigInvoker invoker = config_invoker();
    callbacks.execute = [invoker]() { return camera_resolution_settings::read_resolution(invoker); };
    callbacks.on_complete = [this, request_generation](
                                DComponens::LvglComponensBase::AsyncTaskContext &,
                                const camera_resolution_settings::ReadResult &result) {
        if (!request_is_current(request_generation)) return;
        pending_ = false;
        finish_load(result);
    };
    callbacks.on_exception = [this, request_generation](
                                 DComponens::LvglComponensBase::AsyncTaskContext &,
                                 std::exception_ptr) {
        if (!request_is_current(request_generation)) return;
        pending_ = false;
        finish_load_failure();
    };
    callbacks.on_timeout = [this, request_generation](
                               DComponens::LvglComponensBase::AsyncTaskContext &) {
        if (!request_is_current(request_generation)) return;
        pending_ = false;
        finish_load_failure();
    };
    callbacks.on_schedule_failed = [this, request_generation](
                                       DComponens::LvglComponensBase::AsyncTaskContext &) {
        if (!request_is_current(request_generation)) return;
        pending_ = false;
        finish_load_failure();
    };

    if (!run_async_task(std::move(callbacks)) && request_is_current(request_generation)) {
        pending_ = false;
        finish_load_failure();
    }
}

void LvSettingResolutionPage3::finish_write(const camera_resolution_settings::WriteResult &result)
{
    const int previous_index = camera_resolution_settings::index_for_resolution(result.previous);
    if (previous_index >= 0) {
        saved_resolution_ = result.previous;
        saved_index_ = previous_index;
    }

    if (result.succeeded()) {
        saved_resolution_ = selected_resolution_;
        saved_index_ = camera_resolution_settings::index_for_resolution(saved_resolution_);
        set_status("", false);
        if (LeaveSelfPage) LeaveSelfPage();
        return;
    }

    select(saved_index_);
    restore_focus();
    if (result.status == camera_resolution_settings::WriteStatus::InvalidTarget)
        set_status("Camera resolution is unsupported", true);
    else if (result.status == camera_resolution_settings::WriteStatus::RollbackFailed)
        set_status("Camera resolution rollback failed", true);
    else
        set_status("Camera resolution save failed", true);
}

void LvSettingResolutionPage3::finish_write_failure()
{
    pending_ = false;
    select(saved_index_);
    restore_focus();
    set_status("Camera resolution save failed", true);
}

bool LvSettingResolutionPage3::begin_write()
{
    camera_resolution_settings::Resolution target{};
    if (!camera_resolution_settings::resolution_for_index(selected_index, target)) {
        select(saved_index_);
        restore_focus();
        set_status("Camera resolution is unsupported", true);
        return false;
    }

    pending_ = true;
    selected_resolution_ = target;
    const std::uint64_t request_generation = ++generation_;
    set_status("Saving camera config", false);

    DComponens::LvglComponensBase::AsyncTaskCallbacks<camera_resolution_settings::WriteResult> callbacks;
    const camera_resolution_settings::ConfigInvoker invoker = config_invoker();
    callbacks.execute = [invoker, target]() { return camera_resolution_settings::write_resolution(invoker, target); };
    callbacks.on_complete = [this, request_generation](
                                DComponens::LvglComponensBase::AsyncTaskContext &,
                                const camera_resolution_settings::WriteResult &result) {
        if (!request_is_current(request_generation)) return;
        pending_ = false;
        finish_write(result);
    };
    callbacks.on_exception = [this, request_generation](
                                 DComponens::LvglComponensBase::AsyncTaskContext &,
                                 std::exception_ptr) {
        if (!request_is_current(request_generation)) return;
        finish_write_failure();
    };
    callbacks.on_timeout = [this, request_generation](
                               DComponens::LvglComponensBase::AsyncTaskContext &) {
        if (!request_is_current(request_generation)) return;
        finish_write_failure();
    };
    callbacks.on_schedule_failed = [this, request_generation](
                                       DComponens::LvglComponensBase::AsyncTaskContext &) {
        if (!request_is_current(request_generation)) return;
        finish_write_failure();
    };

    if (!run_async_task(std::move(callbacks)) && request_is_current(request_generation))
        finish_write_failure();
    return true;
}
