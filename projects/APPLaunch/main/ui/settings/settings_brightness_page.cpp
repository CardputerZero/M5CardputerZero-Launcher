#include "settings_brightness_page.hpp"

#include "hal_lvgl_bsp.h"

#if __has_include("model/setup_value_policy.hpp")
#include "model/setup_value_policy.hpp"
#elif __has_include("../../../APPLaunch/main/ui/model/setup_value_policy.hpp")
#include "../../../APPLaunch/main/ui/model/setup_value_policy.hpp"
#endif

#include <algorithm>
#include <array>
#include <climits>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace {

class settings_brightness_com {
    using Page = LvSettingBrightnessPage3;
    using Arguments = Page::Arguments;
    using Callback = Page::Callback;
    using SettingsInvoker = Page::SettingsInvoker;
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
    static constexpr const char *kBrightnessKey = "brightness";
    static constexpr std::chrono::milliseconds kTimeout{3000};

    static Response invoke(const Arguments &arguments, const std::function<void(Arguments, Callback)> &invoker)
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
    static Response get_int(const ConfigInvoker &invoker, int fallback)
    {
        return invoke({"GetInt", kBrightnessKey, std::to_string(fallback)}, invoker);
    }
    static Response set_int(const ConfigInvoker &invoker, int value)
    {
        return invoke({"SetInt", kBrightnessKey, std::to_string(value)}, invoker);
    }
    static Response save(const ConfigInvoker &invoker)
    {
        return invoke({"Save"}, invoker);
    }
    static bool parse_nonnegative(std::string_view text, int &value)
    {
        return setup_values::parse_nonnegative_int(text, value);
    }
    static bool parse_bounded(const Response &response, int minimum, int maximum, int &value)
    {
        return response.succeeded() && parse_nonnegative(response.data, value) && value >= minimum && value <= maximum;
    }
    static bool response_is_ok(const Response &response) { return response.succeeded() && response.data == "ok"; }
    static int normalized_option_count(int count) { return count >= 5 ? 5 : 4; }
    static int brightness_percent(int index, int count)
    {
        static constexpr std::array<int, 4> legacy{{100, 75, 50, 25}};
        static constexpr std::array<int, 5> zero{{100, 75, 50, 25, 0}};
        if (normalized_option_count(count) == 5) return zero[static_cast<std::size_t>(std::clamp(index, 0, 4))];
        return legacy[static_cast<std::size_t>(std::clamp(index, 0, 3))];
    }
    static int brightness_index(int value, int maximum, int count)
    {
        if (normalized_option_count(count) == 4) return setup_values::brightness_index(value, maximum);
        if (maximum <= 0) return 0;
        const int percent = static_cast<int>(static_cast<std::int64_t>(value) * 100 / maximum);
        if (percent >= 88) return 0;
        if (percent >= 63) return 1;
        if (percent >= 38) return 2;
        if (percent >= 13) return 3;
        return 4;
    }
    static int brightness_value(int index, int maximum, int count)
    {
        const int safe_maximum = std::max(1, maximum);
        if (normalized_option_count(count) == 4) return setup_values::brightness_value(index, safe_maximum);
        return static_cast<int>(static_cast<std::int64_t>(safe_maximum) * brightness_percent(index, 5) / 100);
    }
    static bool valid_value(int value, int maximum) { return maximum > 0 && value >= 0 && value <= maximum; }
    static bool valid_index(int index, int count) { const int n = normalized_option_count(count); return index >= 0 && index < n; }
    static bool restore(const SettingsInvoker &settings, const ConfigInvoker &config, int value, int maximum, int config_value)
    {
        int restored = -1;
        const Response hw = invoke({"BacklightWrite", std::to_string(value)}, settings);
        const bool hardware = parse_bounded(hw, 0, maximum, restored) && restored == value;
        const bool config_ok = response_is_ok(set_int(config, config_value));
        const bool save_ok = response_is_ok(save(config));
        return hardware && config_ok && save_ok;
    }

public:
    static Page::BrightnessReadResult read(const SettingsInvoker &settings, const ConfigInvoker &config, int count)
    {
        const Response max_response = invoke({"BacklightMax"}, settings);
        int maximum = 0;
        if (!parse_bounded(max_response, 1, INT_MAX, maximum))
            return {max_response.code == 0 ? Page::BrightnessReadStatus::InvalidPayload : Page::BrightnessReadStatus::BackendError,
                    100, 75, brightness_index(75, 100, count), "backlight maximum read failed"};
        const Response value_response = invoke({"BacklightRead"}, settings);
        int value = 0;
        if (parse_bounded(value_response, 0, maximum, value))
            return {Page::BrightnessReadStatus::Ok, maximum, value, brightness_index(value, maximum, count), {}};
        const Response config_response = get_int(config, maximum);
        int saved = 0;
        if (!parse_bounded(config_response, 0, maximum, saved))
            return {config_response.code == 0 ? Page::BrightnessReadStatus::InvalidPayload : Page::BrightnessReadStatus::BackendError,
                    maximum, maximum, brightness_index(maximum, maximum, count), "backlight and saved brightness read failed"};
        return {Page::BrightnessReadStatus::Defaulted, maximum, saved, brightness_index(saved, maximum, count),
                "backlight read failed; using saved brightness"};
    }
    static Page::BrightnessWriteResult write(const SettingsInvoker &settings, const ConfigInvoker &config,
                                             int index, int maximum, int previous, int count)
    {
        Page::BrightnessWriteResult result;
        result.previous_value = previous;
        result.previous_config = previous;
        if (!valid_value(previous, maximum) || !valid_index(index, count)) {
            result.status = Page::BrightnessWriteStatus::InvalidTarget;
            result.message = "invalid brightness value";
            return result;
        }
        const Response config_response = get_int(config, previous);
        if (!parse_bounded(config_response, 0, maximum, result.previous_config)) {
            result.status = Page::BrightnessWriteStatus::ConfigReadFailed;
            result.message = "brightness config read failed";
            return result;
        }
        const int requested = brightness_value(index, maximum, count);
        if (!valid_value(requested, maximum)) {
            result.status = Page::BrightnessWriteStatus::InvalidTarget;
            result.message = "invalid brightness target";
            return result;
        }
        const Response hardware = invoke({"BacklightWrite", std::to_string(requested)}, settings);
        if (!hardware.succeeded()) {
            result.status = Page::BrightnessWriteStatus::BacklightWriteFailed;
            result.message = "backlight write failed";
            return result;
        }
        int applied = 0;
        if (!parse_bounded(hardware, 0, maximum, applied)) {
            result.status = Page::BrightnessWriteStatus::BacklightPayloadInvalid;
            result.rollback_attempted = true;
            result.rollback_succeeded = restore(settings, config, previous, maximum, result.previous_config);
            if (!result.rollback_succeeded) result.status = Page::BrightnessWriteStatus::RollbackFailed;
            result.message = result.rollback_succeeded ? "invalid backlight write response" : "brightness rollback failed";
            return result;
        }
        result.applied_value = applied;
        result.applied_index = brightness_index(applied, maximum, count);
        if (!response_is_ok(set_int(config, applied))) {
            result.status = Page::BrightnessWriteStatus::ConfigWriteFailed;
            result.rollback_attempted = true;
            result.rollback_succeeded = restore(settings, config, previous, maximum, result.previous_config);
            if (!result.rollback_succeeded) result.status = Page::BrightnessWriteStatus::RollbackFailed;
            result.message = result.rollback_succeeded ? "brightness config write failed" : "brightness rollback failed";
            return result;
        }
        if (!response_is_ok(save(config))) {
            result.status = Page::BrightnessWriteStatus::SaveFailed;
            result.rollback_attempted = true;
            result.rollback_succeeded = restore(settings, config, previous, maximum, result.previous_config);
            if (!result.rollback_succeeded) result.status = Page::BrightnessWriteStatus::RollbackFailed;
            result.message = result.rollback_succeeded ? "brightness save failed" : "brightness rollback failed";
            return result;
        }
        result.status = Page::BrightnessWriteStatus::Ok;
        result.message = "brightness saved";
        return result;
    }
};

} // namespace

bool LvSettingBrightnessPage3::BrightnessReadResult::usable() const noexcept
{
    return status == BrightnessReadStatus::Ok || status == BrightnessReadStatus::Defaulted;
}

bool LvSettingBrightnessPage3::BrightnessReadResult::defaulted() const noexcept
{
    return status == BrightnessReadStatus::Defaulted;
}

bool LvSettingBrightnessPage3::BrightnessWriteResult::succeeded() const noexcept
{
    return status == BrightnessWriteStatus::Ok;
}

LvSettingBrightnessPage3::LvSettingBrightnessPage3() = default;

LvSettingBrightnessPage3::LvSettingBrightnessPage3(lv_obj_t *parent, const NodeIter &parent_node)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize_page(parent, {});
}

LvSettingBrightnessPage3::LvSettingBrightnessPage3(lv_obj_t *parent,
                                                   const NodeIter &parent_node,
                                                   std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {})
{
    initialize_page(parent, std::move(back_callback));
}

LvSettingBrightnessPage3::~LvSettingBrightnessPage3()
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

int LvSettingBrightnessPage3::initial_selection() const
{
    return 0;
}

SettingApiResult LvSettingBrightnessPage3::activate_selected()
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

LvSettingBrightnessPage3::SettingsInvoker LvSettingBrightnessPage3::settings_invoker()
{
    return [](Arguments arguments, Callback callback) {
        cp0_signal_settings_api(std::move(arguments), std::move(callback));
    };
}

LvSettingBrightnessPage3::ConfigInvoker LvSettingBrightnessPage3::config_invoker()
{
    return [](Arguments arguments, Callback callback) {
        cp0_signal_config_api(std::move(arguments), std::move(callback));
    };
}

int LvSettingBrightnessPage3::option_count() const
{
    int count = 0;
    for (auto it = parent_node().begin(); it != parent_node().end(); ++it) ++count;
    return count;
}

void LvSettingBrightnessPage3::initialize_page(lv_obj_t *parent, std::function<void()> back_callback)
{
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
    create_status_label();
    begin_load();
}

void LvSettingBrightnessPage3::request_back()
{
    if (destroying_ || back_requested_) return;
    back_requested_ = true;
    pending_ = false;
    ++generation_;
    cancel_async_tasks();
    if (back_callback_) back_callback_();
}

void LvSettingBrightnessPage3::restore_focus()
{
    if (!ComponensObj) return;
    if (lv_obj_get_group(ComponensObj)) lv_group_focus_obj(ComponensObj);
}

bool LvSettingBrightnessPage3::request_is_current(std::uint64_t generation) const
{
    return page_alive_ && generation_ == generation;
}

void LvSettingBrightnessPage3::create_status_label()
{
    if (!ComponensObj) return;

    status_label_ = lv_label_create(ComponensObj);
    if (!status_label_) return;
    lv_obj_set_width(status_label_, metric(LayoutMetric::StatusLabelW));
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(
        status_label_, cp0_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFFCC66), LV_PART_MAIN);
    lv_obj_set_pos(status_label_, metric(LayoutMetric::StatusLabelX), metric(LayoutMetric::StatusLabelY));
    lv_label_set_text(status_label_, "");
}

void LvSettingBrightnessPage3::set_status(const std::string &text, bool error)
{
    if (!status_label_) return;
    lv_label_set_text(status_label_, text.c_str());
    lv_obj_set_style_text_color(status_label_, lv_color_hex(error ? 0xFF6666 : 0x66CC88), LV_PART_MAIN);
}

void LvSettingBrightnessPage3::finish_load(const BrightnessReadResult &result)
{
    if (!result.usable()) {
        loaded_ = false;
        maximum_ = 100;
        saved_index_ = 0;
        select(saved_index_);
        restore_focus();
        set_status(result.message.empty() ? "Backlight read failed" : result.message, true);
        return;
    }

    loaded_ = true;
    maximum_ = result.maximum;
    value_ = result.value;
    saved_index_ = result.index;
    select(saved_index_);
    restore_focus();
    if (result.defaulted())
        set_status(result.message.empty() ? "Using saved brightness" : result.message, true);
    else
        set_status("", false);
}

void LvSettingBrightnessPage3::finish_load_failure()
{
    loaded_ = false;
    maximum_ = 100;
    saved_index_ = 0;
    select(saved_index_);
    restore_focus();
    set_status("Backlight read failed", true);
}

void LvSettingBrightnessPage3::begin_load()
{
    if (!ComponensObj || pending_) return;

    pending_ = true;
    loaded_ = false;
    const std::uint64_t request_generation = ++generation_;
    const int page_option_count = option_count();
    set_status("Loading brightness", false);

    DComponens::LvglComponensBase::AsyncTaskCallbacks<BrightnessReadResult> callbacks;
    const SettingsInvoker settings = settings_invoker();
    const ConfigInvoker config = config_invoker();
    callbacks.execute = [settings, config, page_option_count]() {
        return settings_brightness_com::read(settings, config, page_option_count);
    };
    callbacks.on_complete = [this, request_generation](
                                DComponens::LvglComponensBase::AsyncTaskContext &,
                                const BrightnessReadResult &result) {
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

    if (!run_async_task(std::move(callbacks)) && pending_ && request_is_current(request_generation)) {
        pending_ = false;
        finish_load_failure();
    }
}

void LvSettingBrightnessPage3::finish_write(const BrightnessWriteResult &result)
{
    pending_ = false;
    if (result.succeeded()) {
        value_ = result.applied_value;
        saved_index_ = result.applied_index;
        select(saved_index_);
        set_status("", false);
        request_back();
        return;
    }

    select(saved_index_);
    restore_focus();
    set_status(result.message.empty() ? "Brightness save failed" : result.message, true);
}

void LvSettingBrightnessPage3::finish_write_failure()
{
    pending_ = false;
    select(saved_index_);
    restore_focus();
    set_status("Brightness save failed", true);
}

bool LvSettingBrightnessPage3::begin_write()
{
    const int page_option_count = option_count();
    if (selected_index < 0 || selected_index >= (page_option_count >= 5 ? 5 : 4)) {
        select(saved_index_);
        restore_focus();
        set_status("Invalid brightness target", true);
        return false;
    }

    pending_ = true;
    const std::uint64_t request_generation = ++generation_;
    set_status("Saving brightness", false);

    DComponens::LvglComponensBase::AsyncTaskCallbacks<BrightnessWriteResult> callbacks;
    const SettingsInvoker settings = settings_invoker();
    const ConfigInvoker config = config_invoker();
    const int target_index = selected_index;
    const int maximum = maximum_;
    const int previous_value = value_;
    callbacks.execute = [settings, config, target_index, maximum, previous_value, page_option_count]() {
        return settings_brightness_com::write(
            settings, config, target_index, maximum, previous_value, page_option_count);
    };
    callbacks.on_complete = [this, request_generation](
                                DComponens::LvglComponensBase::AsyncTaskContext &,
                                const BrightnessWriteResult &result) {
        if (!request_is_current(request_generation)) return;
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

    if (!run_async_task(std::move(callbacks)) && pending_ && request_is_current(request_generation))
        finish_write_failure();
    return true;
}
