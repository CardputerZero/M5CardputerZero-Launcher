#include "settings_screen_timeout_page.hpp"
#include "settings_fonts.hpp"

#if __has_include("model/setup_value_policy.hpp")
#include "model/setup_value_policy.hpp"
#elif __has_include("../../../APPLaunch/main/ui/model/setup_value_policy.hpp")
#include "../../../APPLaunch/main/ui/model/setup_value_policy.hpp"
#endif

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string_view>
#include <exception>
#include <utility>

namespace {

class settings_dark_time_com {
    using Page = LvSettingDarkTimePage3;
    using Arguments = Page::Arguments;
    using Callback = Page::Callback;
    using ConfigInvoker = Page::ConfigInvoker;
    struct Response {
        int code = -1;
        std::string data;
        bool succeeded() const noexcept { return code == 0; }
    };
    struct WaitState {
        std::mutex mutex;
        std::condition_variable condition;
        bool completed = false;
        int code = -1;
        std::string data;
    };
    static constexpr const char *kDarkTimeKey = "dark_time";
    static constexpr std::chrono::milliseconds kTimeout{3000};

    static Response invoke(const Arguments &arguments, const ConfigInvoker &invoker)
    {
        if (!invoker) return {-1, "screen settings invoker unavailable"};
        auto state = std::make_shared<WaitState>();
        try {
            invoker(arguments, [state](int code, std::string data) {
                try {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->completed) return;
                    state->code = code;
                    state->data = std::move(data);
                    state->completed = true;
                    state->condition.notify_one();
                } catch (...) {
                }
            });
        } catch (...) {
            return {-1, "screen settings invocation failed"};
        }
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->condition.wait_for(lock, kTimeout, [state] { return state->completed; }))
            return {-1, "screen settings api timeout"};
        return {state->code, std::move(state->data)};
    }
    static Response get_int(const ConfigInvoker &invoker)
    {
        return invoke({"GetInt", kDarkTimeKey,
                       std::to_string(static_cast<int>(Page::DefaultValue::DarkTimeSeconds))},
                      invoker);
    }
    static Response set_int(const ConfigInvoker &invoker, int value)
    {
        return invoke({"SetInt", kDarkTimeKey, std::to_string(value)}, invoker);
    }
    static Response save(const ConfigInvoker &invoker) { return invoke({"Save"}, invoker); }
    static bool response_is_ok(const Response &response) { return response.succeeded() && response.data == "ok"; }
    static bool parse_nonnegative(std::string_view text, int &value)
    {
        return setup_values::parse_nonnegative_int(text, value);
    }
    static int dark_time_index(int seconds) { return setup_values::dark_time_index(seconds); }
    static int dark_time_seconds(int index) { return setup_values::dark_time_seconds(index); }
    static bool valid_index(int index) { return index >= 0 && index < 5; }
    static bool restore(const ConfigInvoker &invoker, int value)
    {
        return response_is_ok(set_int(invoker, value)) && response_is_ok(save(invoker));
    }

public:
    static Page::DarkTimeReadResult read(const ConfigInvoker &invoker)
    {
        const Response response = get_int(invoker);
        int seconds = 0;
        if (!response.succeeded())
            return {Page::DarkTimeReadStatus::BackendError,
                    static_cast<int>(Page::DefaultValue::DarkTimeSeconds), 2,
                    "dark time read failed"};
        if (!parse_nonnegative(response.data, seconds)) {
            const int default_seconds = static_cast<int>(Page::DefaultValue::DarkTimeSeconds);
            return {Page::DarkTimeReadStatus::Defaulted, default_seconds,
                    dark_time_index(default_seconds), "invalid dark time; using 30S"};
        }
        const int index = dark_time_index(seconds);
        const int normalized = dark_time_seconds(index);
        if (normalized != seconds)
            return {Page::DarkTimeReadStatus::Defaulted, normalized, index, "unsupported dark time; using 30S"};
        return {Page::DarkTimeReadStatus::Ok, seconds, index, {}};
    }
    static Page::DarkTimeWriteResult write(const ConfigInvoker &invoker, int index)
    {
        Page::DarkTimeWriteResult result;
        if (!valid_index(index)) {
            result.status = Page::DarkTimeWriteStatus::InvalidTarget;
            result.message = "invalid dark time target";
            return result;
        }
        const Page::DarkTimeReadResult previous = read(invoker);
        if (!previous.usable()) {
            result.status = Page::DarkTimeWriteStatus::ReadFailed;
            result.message = "dark time read failed";
            return result;
        }
        result.previous_seconds = previous.seconds;
        result.previous_index = previous.index;
        result.applied_seconds = dark_time_seconds(index);
        if (!response_is_ok(set_int(invoker, result.applied_seconds))) {
            result.status = Page::DarkTimeWriteStatus::SetFailed;
            result.rollback_attempted = true;
            result.rollback_succeeded = restore(invoker, result.previous_seconds);
            if (!result.rollback_succeeded) result.status = Page::DarkTimeWriteStatus::RollbackFailed;
            result.message = result.rollback_succeeded ? "dark time write failed" : "dark time rollback failed";
            return result;
        }
        if (!response_is_ok(save(invoker))) {
            result.status = Page::DarkTimeWriteStatus::SaveFailed;
            result.rollback_attempted = true;
            result.rollback_succeeded = restore(invoker, result.previous_seconds);
            if (!result.rollback_succeeded) result.status = Page::DarkTimeWriteStatus::RollbackFailed;
            result.message = result.rollback_succeeded ? "dark time save failed" : "dark time rollback failed";
            return result;
        }
        result.status = Page::DarkTimeWriteStatus::Ok;
        result.message = "dark time saved";
        return result;
    }
};

} // namespace

bool LvSettingDarkTimePage3::DarkTimeReadResult::usable() const noexcept
{
    return status == DarkTimeReadStatus::Ok || status == DarkTimeReadStatus::Defaulted;
}

bool LvSettingDarkTimePage3::DarkTimeReadResult::defaulted() const noexcept
{
    return status == DarkTimeReadStatus::Defaulted;
}

bool LvSettingDarkTimePage3::DarkTimeWriteResult::succeeded() const noexcept
{
    return status == DarkTimeWriteStatus::Ok;
}

LvSettingDarkTimePage3::LvSettingDarkTimePage3() = default;

LvSettingDarkTimePage3::LvSettingDarkTimePage3(lv_obj_t *parent, const NodeIter &parent_node)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize_page(parent, {});
}

LvSettingDarkTimePage3::LvSettingDarkTimePage3(lv_obj_t *parent,
                                               const NodeIter &parent_node,
                                               std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize_page(parent, std::move(back_callback));
}

LvSettingDarkTimePage3::~LvSettingDarkTimePage3()
{
    destroying_ = true;
    page_alive_ = false;
    ++generation_;
    pending_ = false;
    LeaveSelfPage = nullptr;
    cancel_async_tasks();
    back_callback_ = nullptr;
    status_label_ = nullptr;
}

int LvSettingDarkTimePage3::initial_selection() const
{
    return 2;
}

SettingApiResult LvSettingDarkTimePage3::activate_selected()
{
    if (pending_) return SettingApiResult::Pending;
    if (!loaded_) {
        begin_load();
        return SettingApiResult::Pending;
    }
    if (selected_index == saved_index_) {
        request_back();
        return SettingApiResult::Success;
    }
    return begin_write() ? SettingApiResult::Pending : SettingApiResult::Failure;
}

LvSettingDarkTimePage3::ConfigInvoker LvSettingDarkTimePage3::config_invoker()
{
    return [](Arguments arguments, Callback callback) {
        cp0_signal_config_api(std::move(arguments), std::move(callback));
    };
}

void LvSettingDarkTimePage3::initialize_page(lv_obj_t *parent, std::function<void()> back_callback)
{
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
    create_status_label();
    begin_load();
}

void LvSettingDarkTimePage3::request_back()
{
    if (destroying_ || back_requested_) return;
    back_requested_ = true;
    pending_ = false;
    ++generation_;
    cancel_async_tasks();
    if (back_callback_) back_callback_();
}

void LvSettingDarkTimePage3::restore_focus()
{
    if (!ComponensObj) return;
    if (lv_obj_get_group(ComponensObj)) lv_group_focus_obj(ComponensObj);
}

bool LvSettingDarkTimePage3::request_is_current(std::uint64_t generation) const
{
    return page_alive_ && generation_ == generation;
}

void LvSettingDarkTimePage3::create_status_label()
{
    if (!ComponensObj) return;

    status_label_ = lv_label_create(ComponensObj);
    if (!status_label_) return;
    lv_obj_set_width(status_label_, 104);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(
        status_label_, settings_fonts::sans(10, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFFCC66), LV_PART_MAIN);
    lv_obj_set_pos(status_label_, 4, 4);
    lv_label_set_text(status_label_, "");
}

void LvSettingDarkTimePage3::set_status(const std::string &text, bool error)
{
    if (!status_label_) return;
    lv_label_set_text(status_label_, text.c_str());
    lv_obj_set_style_text_color(status_label_, lv_color_hex(error ? 0xFF6666 : 0x66CC88), LV_PART_MAIN);
}

void LvSettingDarkTimePage3::finish_load(const DarkTimeReadResult &result)
{
    if (!result.usable()) {
        loaded_ = false;
        saved_index_ = 2;
        select(saved_index_);
        restore_focus();
        set_status(result.message.empty() ? "Dark time read failed" : result.message, true);
        return;
    }

    loaded_ = true;
    saved_seconds_ = result.seconds;
    saved_index_ = result.index;
    select(saved_index_);
    restore_focus();
    if (result.defaulted())
        set_status(result.message.empty() ? "Using 30S" : result.message, true);
    else
        set_status("", false);
}

void LvSettingDarkTimePage3::finish_load_failure()
{
    loaded_ = false;
    saved_seconds_ = static_cast<int>(DefaultValue::DarkTimeSeconds);
    saved_index_ = 2;
    select(saved_index_);
    restore_focus();
    set_status("Dark time read failed", true);
}

void LvSettingDarkTimePage3::begin_load()
{
    if (!ComponensObj || pending_) return;

    pending_ = true;
    loaded_ = false;
    const std::uint64_t request_generation = ++generation_;
    set_status("Loading dark time", false);

    DComponens::LvglComponensBase::AsyncTaskCallbacks<DarkTimeReadResult> callbacks;
    const ConfigInvoker config = config_invoker();
    callbacks.execute = [config]() { return settings_dark_time_com::read(config); };
    callbacks.on_complete = [this, request_generation](
                                DComponens::LvglComponensBase::AsyncTaskContext &,
                                const DarkTimeReadResult &result) {
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
    callbacks.on_timeout = [this, request_generation](DComponens::LvglComponensBase::AsyncTaskContext &) {
        if (!request_is_current(request_generation)) return;
        pending_ = false;
        finish_load_failure();
    };
    callbacks.on_schedule_failed = [this, request_generation](DComponens::LvglComponensBase::AsyncTaskContext &) {
        if (!request_is_current(request_generation)) return;
        pending_ = false;
        finish_load_failure();
    };

    if (!run_async_task(std::move(callbacks)) && pending_ && request_is_current(request_generation)) {
        pending_ = false;
        finish_load_failure();
    }
}

void LvSettingDarkTimePage3::finish_write(const DarkTimeWriteResult &result)
{
    pending_ = false;
    if (result.succeeded()) {
        saved_seconds_ = result.applied_seconds;
        saved_index_ = setup_values::dark_time_index(result.applied_seconds);
        select(saved_index_);
        set_status("", false);
        request_back();
        return;
    }

    select(saved_index_);
    restore_focus();
    set_status(result.message.empty() ? "Dark time save failed" : result.message, true);
}

void LvSettingDarkTimePage3::finish_write_failure()
{
    pending_ = false;
    select(saved_index_);
    restore_focus();
    set_status("Dark time save failed", true);
}

bool LvSettingDarkTimePage3::begin_write()
{
    if (selected_index < 0 || selected_index >= 5) {
        select(saved_index_);
        restore_focus();
        set_status("Invalid dark time target", true);
        return false;
    }

    pending_ = true;
    const std::uint64_t request_generation = ++generation_;
    set_status("Saving dark time", false);

    DComponens::LvglComponensBase::AsyncTaskCallbacks<DarkTimeWriteResult> callbacks;
    const ConfigInvoker config = config_invoker();
    const int target_index = selected_index;
    callbacks.execute = [config, target_index]() {
        return settings_dark_time_com::write(config, target_index);
    };
    callbacks.on_complete = [this, request_generation](
                                DComponens::LvglComponensBase::AsyncTaskContext &,
                                const DarkTimeWriteResult &result) {
        if (!request_is_current(request_generation)) return;
        finish_write(result);
    };
    callbacks.on_exception = [this, request_generation](
                                 DComponens::LvglComponensBase::AsyncTaskContext &,
                                 std::exception_ptr) {
        if (!request_is_current(request_generation)) return;
        finish_write_failure();
    };
    callbacks.on_timeout = [this, request_generation](DComponens::LvglComponensBase::AsyncTaskContext &) {
        if (!request_is_current(request_generation)) return;
        finish_write_failure();
    };
    callbacks.on_schedule_failed = [this, request_generation](DComponens::LvglComponensBase::AsyncTaskContext &) {
        if (!request_is_current(request_generation)) return;
        finish_write_failure();
    };

    if (!run_async_task(std::move(callbacks)) && pending_ && request_is_current(request_generation))
        finish_write_failure();
    return true;
}
