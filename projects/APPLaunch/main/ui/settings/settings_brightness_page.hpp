#pragma once

#include "hal_lvgl_bsp.h"
#include "settings_screen_api.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <utility>

#include "lvgl_components.hpp"

namespace screen_settings = settings_screen;

class LvSettingBrightnessPage3 : public LvSettingValuePage3Base {
public:
    LvSettingBrightnessPage3() = default;

    LvSettingBrightnessPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize_page(parent, {});
    }

    LvSettingBrightnessPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize_page(parent, std::move(back_callback));
    }

    ~LvSettingBrightnessPage3() override
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

protected:
    int initial_selection() const override
    {
        return 0;
    }

    SettingApiResult activate_selected() override
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

private:
    static screen_settings::SettingsInvoker settings_invoker()
    {
        return [](screen_settings::Arguments arguments, screen_settings::Callback callback) {
            cp0_signal_settings_api(std::move(arguments), std::move(callback));
        };
    }

    static screen_settings::ConfigInvoker config_invoker()
    {
        return [](screen_settings::Arguments arguments, screen_settings::Callback callback) {
            cp0_signal_config_api(std::move(arguments), std::move(callback));
        };
    }

    int option_count() const
    {
        int count = 0;
        for (auto it = parent_node().begin(); it != parent_node().end(); ++it) ++count;
        return count;
    }

    void initialize_page(lv_obj_t *parent, std::function<void()> back_callback)
    {
        back_callback_ = std::move(back_callback);
        LeaveSelfPage = [this] { request_back(); };
        initialize(parent);
        create_status_label();
        begin_load();
    }

    void request_back()
    {
        if (destroying_ || back_requested_) return;
        back_requested_ = true;
        pending_ = false;
        ++generation_;
        cancel_async_tasks();
        if (back_callback_) back_callback_();
    }

    void restore_focus()
    {
        if (!ComponensObj) return;
        if (lv_obj_get_group(ComponensObj)) lv_group_focus_obj(ComponensObj);
    }

    bool request_is_current(std::uint64_t generation) const
    {
        return page_alive_ && generation_ == generation;
    }

    void create_status_label()
    {
        if (!ComponensObj) return;

        status_label_ = lv_label_create(ComponensObj);
        if (!status_label_) return;
        lv_obj_set_width(status_label_, 104);
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(
            status_label_, cp0_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFFCC66), LV_PART_MAIN);
        lv_obj_set_pos(status_label_, 4, 4);
        lv_label_set_text(status_label_, "");
    }

    void set_status(const std::string &text, bool error)
    {
        if (!status_label_) return;
        lv_label_set_text(status_label_, text.c_str());
        lv_obj_set_style_text_color(status_label_, lv_color_hex(error ? 0xFF6666 : 0x66CC88), LV_PART_MAIN);
    }

    void finish_load(const screen_settings::BrightnessReadResult &result)
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

    void finish_load_failure()
    {
        loaded_ = false;
        maximum_ = 100;
        saved_index_ = 0;
        select(saved_index_);
        restore_focus();
        set_status("Backlight read failed", true);
    }

    void begin_load()
    {
        if (!ComponensObj || pending_) return;

        pending_ = true;
        loaded_ = false;
        const std::uint64_t request_generation = ++generation_;
        const int page_option_count = option_count();
        set_status("Loading brightness", false);

        DComponens::LvglComponensBase::AsyncTaskCallbacks<screen_settings::BrightnessReadResult> callbacks;
        const screen_settings::SettingsInvoker settings = settings_invoker();
        const screen_settings::ConfigInvoker config = config_invoker();
        callbacks.execute = [settings, config, page_option_count]() {
            return screen_settings::read_brightness(settings, config, page_option_count);
        };
        callbacks.on_complete = [this, request_generation](
                                    DComponens::LvglComponensBase::AsyncTaskContext &,
                                    const screen_settings::BrightnessReadResult &result) {
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

    void finish_write(const screen_settings::BrightnessWriteResult &result)
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

    void finish_write_failure()
    {
        pending_ = false;
        select(saved_index_);
        restore_focus();
        set_status("Brightness save failed", true);
    }

    bool begin_write()
    {
        const int page_option_count = option_count();
        if (!screen_settings::brightness_index_valid(selected_index, page_option_count)) {
            select(saved_index_);
            restore_focus();
            set_status("Invalid brightness target", true);
            return false;
        }

        pending_ = true;
        const std::uint64_t request_generation = ++generation_;
        set_status("Saving brightness", false);

        DComponens::LvglComponensBase::AsyncTaskCallbacks<screen_settings::BrightnessWriteResult> callbacks;
        const screen_settings::SettingsInvoker settings = settings_invoker();
        const screen_settings::ConfigInvoker config = config_invoker();
        const int target_index = selected_index;
        const int maximum = maximum_;
        const int previous_value = value_;
        callbacks.execute = [settings, config, target_index, maximum, previous_value, page_option_count]() {
            return screen_settings::write_brightness(
                settings, config, target_index, maximum, previous_value, page_option_count);
        };
        callbacks.on_complete = [this, request_generation](
                                    DComponens::LvglComponensBase::AsyncTaskContext &,
                                    const screen_settings::BrightnessWriteResult &result) {
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

    bool destroying_ = false;
    bool back_requested_ = false;
    bool page_alive_ = true;
    bool pending_ = false;
    bool loaded_ = false;
    std::uint64_t generation_ = 0;
    int maximum_ = 100;
    int value_ = 75;
    int saved_index_ = 0;
    lv_obj_t *status_label_ = nullptr;
    std::function<void()> back_callback_;
};
