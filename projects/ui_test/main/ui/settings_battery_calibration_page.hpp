#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "lvgl_components.hpp"
#include "settings_battery_api.hpp"

class LvSettingBQCalibratePage3 : public LvSettingValuePage3Base {
public:
    LvSettingBQCalibratePage3()
        : api_(),
          requests_(api_, [this](std::function<void()> task) {
              return post_to_lvgl(std::move(task));
          })
    {
    }

    LvSettingBQCalibratePage3(
        lv_obj_t *parent,
        const NodeIter &parent_node,
        SettingsBatteryApi api = SettingsBatteryApi{},
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1800))
        : LvSettingValuePage3Base(parent_node, {}),
          api_(std::move(api)),
          requests_(api_, [this](std::function<void()> task) {
              return post_to_lvgl(std::move(task));
          }, timeout)
    {
        initialize(parent);
    }

    LvSettingBQCalibratePage3(
        lv_obj_t *parent,
        const NodeIter &parent_node,
        std::function<void()> back_callback,
        SettingsBatteryApi api = SettingsBatteryApi{},
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1800))
        : LvSettingValuePage3Base(parent_node, std::move(back_callback)),
          api_(std::move(api)),
          requests_(api_, [this](std::function<void()> task) {
              return post_to_lvgl(std::move(task));
          }, timeout)
    {
        initialize(parent);
    }

    ~LvSettingBQCalibratePage3() override
    {
        requests_.cancel();
        requests_.shutdown();
        cancel_async_tasks();
        result_label_ = nullptr;
    }

    void create_ui(lv_obj_t *parent) override
    {
        LvSettingValuePage3Base::create_ui(parent);
        async_token_ = async_token();
        if (!ComponensObj) return;

        result_label_ = lv_label_create(ComponensObj);
        if (!result_label_) return;
        lv_label_set_text(result_label_, "Ready");
        lv_obj_set_pos(result_label_, 8, 120);
        lv_obj_set_size(result_label_, 304, 15);
        lv_obj_set_style_text_color(result_label_, lv_color_hex(0xF0C850), LV_PART_MAIN);
        const lv_font_t *font = cp0_fonts().get(
            "Montserrat-Bold.ttf",
            11,
            LV_FREETYPE_FONT_STYLE_BOLD);
        if (font) lv_obj_set_style_text_font(result_label_, font, LV_PART_MAIN);
        lv_label_set_long_mode(result_label_, LV_LABEL_LONG_CLIP);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }

    SettingApiResult activate_selected() override
    {
        const auto count = std::distance(parent_node().begin(), parent_node().end());
        if (selected_index < 0 || selected_index >= count) return SettingApiResult::NotHandled;
        if (requests_.pending()) return SettingApiResult::Pending;

        set_result("Sending calibration...");
        if (!requests_.calibrate(
                selected_index,
                [this](const SettingsBatteryOperationResult &result) {
                    handle_result(result);
            })) {
            set_result("Calibration unavailable");
            return SettingApiResult::Failure;
        }
        active_generation_ = requests_.generation();
        return SettingApiResult::Pending;
    }

private:
    bool post_to_lvgl(std::function<void()> task)
    {
        if (!task || !async_token_.valid()) return false;
        return SettingsAsync::Dispatch::enqueue_from_callback(
            async_token_, std::move(task));
    }

    void set_result(const std::string &text)
    {
        if (result_label_) lv_label_set_text(result_label_, text.c_str());
    }

    void handle_result(const SettingsBatteryOperationResult &result)
    {
        if (result.operation != SettingsBatteryOperation::Calibrate ||
            result.generation != active_generation_)
            return;

        if (result.outcome == SettingsBatteryOutcome::Success && result.code == 0) {
            set_result("Calibration complete");
        } else if (result.outcome == SettingsBatteryOutcome::TimedOut) {
            set_result("Calibration timed out");
        } else if (result.outcome == SettingsBatteryOutcome::Cancelled) {
            set_result("Calibration cancelled");
        } else {
            set_result("Calibration failed");
        }
    }

    SettingsBatteryApi api_;
    SettingsBatteryRequestCoordinator requests_;
    lv_obj_t *result_label_ = nullptr;
    std::uint64_t active_generation_ = 0;
    AsyncToken async_token_;
};
