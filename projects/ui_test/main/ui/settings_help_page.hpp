#pragma once

#include <functional>
#include <utility>

#include "cp0_font_service.hpp"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

class LvSettingHelpPage3 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        ScreenW = 320,
        ScreenH = 150,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingHelpPage3() = default;

    LvSettingHelpPage3(lv_obj_t *parent,
                       const NodeIter &page_node,
                       std::function<void()> back_callback)
        : page_node_(page_node)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
    }

    ~LvSettingHelpPage3() override
    {
        if (ComponensObj) {
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
        if (LeaveSelfPage) LeaveSelfPage();
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;

        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj, metric(LayoutMetric::ScreenW), metric(LayoutMetric::ScreenH));
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
            std::bind(&LvSettingHelpPage3::handle_key_event, this, std::placeholders::_1));

        const lv_font_t *title_font =
            cp0_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD);
        const lv_font_t *text_font = &lv_font_montserrat_10;
        add_label(8, 2, "Help", 0x58A6FF, title_font ? title_font : &lv_font_montserrat_12);
        add_label(8, 29, "Screenshot: PrtSc / Ctrl+Alt+S", 0xECECEC, text_font);
        add_label(8, 45, "Saved to ~/Pictures/Screenshots", 0x888888, text_font);
        add_label(8, 61, "Home: Hold ESC 3s", 0xECECEC, text_font);
        add_label(8, 77, "Navigate: Arrow keys / OK / ESC", 0xECECEC, text_font);
        add_label(8, 93, "WiFi: Setting > WiFi > Scan", 0xECECEC, text_font);
        add_label(8, 128, "ESC: back", 0x666666, text_font);
    }

private:
    static lv_obj_t *create_label(lv_obj_t *parent,
                                  int pos_x,
                                  int pos_y,
                                  const char *text,
                                  uint32_t color,
                                  const lv_font_t *font)
    {
        if (!parent) return nullptr;
        lv_obj_t *label = lv_label_create(parent);
        if (!label) return nullptr;
        lv_label_set_text(label, text);
        lv_obj_set_pos(label, pos_x, pos_y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        return label;
    }

    void add_label(int pos_x,
                   int pos_y,
                   const char *text,
                   uint32_t color,
                   const lv_font_t *font)
    {
        create_label(ComponensObj, pos_x, pos_y, text, color, font);
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (LeaveSelfPage) LeaveSelfPage();
        }
        lv_event_stop_processing(event);
    }

    NodeIter page_node_;
};
