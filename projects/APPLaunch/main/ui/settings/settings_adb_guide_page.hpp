#pragma once

#include <functional>
#include <memory>

#include "cp0_font_service.hpp"
#include "settings_async_dispatch.hpp"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

namespace settings_adb {
class AdbApi;
}

class LvSettingAdbGuidePage3 : public DComponens::LvglComponensBase {
public:
    // Legacy settings-tree callback for the ADB toggle entry.  The
    // implementation lives with the ADB page and its communication helper.
    static void toggle_setting(int command, void *data);

    enum class LayoutMetric : int {
        ScreenW = 320,
        ScreenH = 150,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingAdbGuidePage3();

    LvSettingAdbGuidePage3(lv_obj_t *parent,
                           const NodeIter &page_node,
                           std::function<void()> back_callback,
                           bool enabling = true);

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;

    ~LvSettingAdbGuidePage3() override;

    void create_ui(lv_obj_t *parent) override;

private:
    static lv_obj_t *create_chip(lv_obj_t *parent, int pos_x, int pos_y, int width, int height,
                                 uint32_t background, uint32_t border, int radius, int border_width);
    static lv_obj_t *create_label(lv_obj_t *parent, int pos_x, int pos_y, const char *text,
                                  uint32_t color, const lv_font_t *font);
    lv_obj_t *add_chip(int pos_x, int pos_y, int width, int height, uint32_t background,
                       uint32_t border, int radius, int border_width);
    lv_obj_t *add_label(int pos_x, int pos_y, const char *text, uint32_t color,
                        const lv_font_t *font);
    void stop_animation();
    void start_animation();
    void render_guide();
    static void api_timer_cb(lv_timer_t *timer) noexcept;
    void request_status();
    void request_reboot();
    void leave_page();
    void handle_key_event(lv_event_t *event);

    NodeIter page_node_;
    lv_obj_t *knob_ = nullptr;
    lv_obj_t *title_label_ = nullptr;
    lv_obj_t *usb_label_ = nullptr;
    lv_obj_t *hub_label_ = nullptr;
    lv_obj_t *step_one_label_ = nullptr;
    lv_obj_t *step_two_label_ = nullptr;
    lv_obj_t *step_three_label_ = nullptr;
    lv_obj_t *confirm_label_ = nullptr;
    lv_timer_t *api_timer_ = nullptr;
    std::unique_ptr<settings_adb::AdbApi> adb_api_;
    SettingsAsync::Dispatch dispatch_;
    bool enabling_ = true;
    bool status_pending_ = false;
    bool reboot_pending_ = false;
    bool reboot_requested_ = false;
    bool leaving_ = false;
};
