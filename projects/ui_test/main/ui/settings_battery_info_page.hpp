#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "cp0_font_service.hpp"
#include "lvgl_components.hpp"
#include "settings_battery_api.hpp"
#include "settings_battery_info_model.hpp"

class LvSettingBatteryInfoPage3 : public DComponens::LvglComponensBase {
public:
    LvSettingBatteryInfoPage3()
        : api_(),
          requests_(api_, [this](std::function<void()> task) {
              return post_to_lvgl(std::move(task));
          })
    {
    }

    LvSettingBatteryInfoPage3(
        lv_obj_t *parent,
        const NodeIter &parent_node,
        std::function<void()> back_callback,
        SettingsBatteryApi api = SettingsBatteryApi{},
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1800))
        : parent_node_(parent_node),
          api_(std::move(api)),
          requests_(api_, [this](std::function<void()> task) {
              return post_to_lvgl(std::move(task));
          }, timeout)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
    }

    ~LvSettingBatteryInfoPage3() override
    {
        stop_refresh_timer();
        requests_.cancel();
        requests_.shutdown();
        cancel_async_tasks();
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
    }

    void AnimateNextIn(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }

    void AnimateNextOut(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }

    void LoadNextPage() override {}

    void LeaveNextPage() override
    {
        requests_.cancel();
        if (LeaveSelfPage) LeaveSelfPage();
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;
        const bool dispatch_ready = ensure_async_dispatch();
        async_token_ = async_token();

        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj, 320, 150);
        lv_obj_set_pos(ComponensObj, 0, 0);
        lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
        DComponens::lvgl_bind_event(
            ComponensObj,
            LV_EVENT_KEY,
            nullptr,
            std::bind(&LvSettingBatteryInfoPage3::handle_key_event,
                      this,
                      std::placeholders::_1));

        title_label_ = create_label(8, 2, 304, 15, 0xFFFFFF, 13);
        if (title_label_) lv_label_set_text(title_label_, "Battery Info");

        for (std::size_t index = 0; index < value_labels_.size(); ++index) {
            value_labels_[index] = create_label(
                12,
                19 + static_cast<int>(index) * 16,
                296,
                15,
                index == 0 ? 0xFFFFFF : 0xB8B8B8,
                index == 0 ? 12 : 11);
        }

        status_label_ = create_label(8, 117, 304, 15, 0xF0C850, 11);
        hint_label_ = create_label(8, 135, 304, 13, 0x777777, 10);
        if (hint_label_) lv_label_set_text(hint_label_, "OK: refresh  ESC: back");

        refresh_timer_ = lv_timer_create(refresh_timer_cb, 1000, this);
        if (refresh_timer_) lv_timer_ready(refresh_timer_);
        else status_message_ = "Refresh unavailable";
        if (!dispatch_ready || !async_token_.valid())
            status_message_ = "Async refresh unavailable";

        render();
        if (dispatch_ready && async_token_.valid()) request_read();
    }

private:
    bool post_to_lvgl(std::function<void()> task)
    {
        if (!task || !async_token_.valid()) return false;
        return SettingsAsync::Dispatch::enqueue_from_callback(
            async_token_, std::move(task));
    }

    static void refresh_timer_cb(lv_timer_t *timer)
    {
        auto *self = timer ? static_cast<LvSettingBatteryInfoPage3 *>(lv_timer_get_user_data(timer)) : nullptr;
        if (self) self->request_read();
    }

    lv_obj_t *create_label(int x,
                           int y,
                           int width,
                           int height,
                           std::uint32_t color,
                           int font_size)
    {
        if (!ComponensObj) return nullptr;
        lv_obj_t *label = lv_label_create(ComponensObj);
        if (!label) return nullptr;
        lv_label_set_text(label, "");
        lv_obj_set_pos(label, x, y);
        lv_obj_set_size(label, width, height);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        const lv_font_t *font = cp0_fonts().get(
            "Montserrat-Bold.ttf",
            font_size,
            LV_FREETYPE_FONT_STYLE_BOLD);
        if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        return label;
    }

    void request_read()
    {
        if (!ComponensObj || !async_token_.valid() || requests_.pending()) return;

        model_.invalidate("Reading battery...");
        status_message_ = "Reading battery...";
        render();

        if (!requests_.read([this](const SettingsBatteryOperationResult &result) {
                handle_read_result(result);
            })) {
            model_.invalidate("Battery read unavailable");
            status_message_ = "Battery read unavailable";
            render();
            return;
        }
        active_generation_ = requests_.generation();
    }

    void handle_read_result(const SettingsBatteryOperationResult &result)
    {
        if (result.operation != SettingsBatteryOperation::Read ||
            result.generation != active_generation_)
            return;

        if (result.outcome == SettingsBatteryOutcome::Success &&
            model_.update(result.code, result.payload)) {
            status_message_ = "Battery updated";
        } else if (result.outcome == SettingsBatteryOutcome::TimedOut) {
            model_.invalidate("Battery read timed out");
            status_message_ = "Battery read timed out";
        } else if (result.outcome == SettingsBatteryOutcome::Cancelled) {
            model_.invalidate("Battery read cancelled");
            status_message_ = "Battery read cancelled";
        } else {
            model_.invalidate(result.code == 0 ? "Invalid battery data"
                                               : "Battery read failed");
            status_message_ = model_.status_text();
        }
        render();
    }

    void render()
    {
        const auto &labels = model_.labels();
        for (std::size_t index = 0; index < value_labels_.size(); ++index) {
            if (value_labels_[index]) lv_label_set_text(value_labels_[index], labels[index].c_str());
        }
        if (status_label_) lv_label_set_text(status_label_, status_message_.c_str());
    }

    void stop_refresh_timer()
    {
        if (!refresh_timer_) return;
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            requests_.cancel();
            if (LeaveSelfPage) LeaveSelfPage();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            request_read();
        }
        lv_event_stop_processing(event);
    }

    NodeIter parent_node_{};
    SettingsBatteryApi api_;
    SettingsBatteryRequestCoordinator requests_;
    SettingsBatteryInfoModel model_;
    std::array<lv_obj_t *, SettingsBatteryInfoModel::kLabelCount> value_labels_{};
    lv_obj_t *title_label_ = nullptr;
    lv_obj_t *status_label_ = nullptr;
    lv_obj_t *hint_label_ = nullptr;
    lv_timer_t *refresh_timer_ = nullptr;
    std::uint64_t active_generation_ = 0;
    AsyncToken async_token_;
    std::string status_message_ = "Battery unavailable";
};

inline std::unique_ptr<DComponens::LvglComponensBase> settings_battery_info_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> on_back)
{
    return std::make_unique<LvSettingBatteryInfoPage3>(
        parent,
        page_node,
        std::move(on_back));
}
