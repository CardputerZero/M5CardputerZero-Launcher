#pragma once

#include <functional>
#include <utility>

#include "cp0_font_service.hpp"
#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

class LvSettingAdbGuidePage3 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        ScreenW = 320,
        ScreenH = 150,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingAdbGuidePage3() = default;

    LvSettingAdbGuidePage3(lv_obj_t *parent,
                           const NodeIter &page_node,
                           std::function<void()> back_callback)
        : page_node_(page_node)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
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

    ~LvSettingAdbGuidePage3() override
    {
        stop_animation();
        if (ComponensObj) {
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;

        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj,
                        metric(LayoutMetric::ScreenW),
                        metric(LayoutMetric::ScreenH));
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
            std::bind(&LvSettingAdbGuidePage3::handle_key_event, this, std::placeholders::_1));

        const lv_font_t *title_font =
            cp0_fonts().get("Montserrat-Bold.ttf", 13, LV_FREETYPE_FONT_STYLE_BOLD);
        const lv_font_t *text_font = &lv_font_montserrat_10;
        add_label(8, 2, "Enable ADB - switch USB to device", 0xECECEC,
                  title_font ? title_font : &lv_font_montserrat_12);
        add_chip(86, 24, 146, 50, 0x282A30, 0x5A5C64, 6, 2);
        add_label(120, 28, "CardputerZero", 0x9A9AA0, text_font);
        add_chip(218, 30, 12, 12, 0x101012, 0x5A5C64, 3, 2);
        add_chip(228, 32, 22, 8, 0xCDCDD2, 0xCDCDD2, 2, 0);
        add_chip(250, 34, 60, 4, 0x6A6C72, 0x6A6C72, 2, 0);
        add_label(232, 42, "USB-C", 0x46DC87, text_font);
        add_chip(24, 28, 32, 44, 0x1A1A1C, 0x5A5C64, 6, 2);
        add_chip(33, 33, 14, 34, 0x0E0E10, 0x0E0E10, 4, 0);
        add_label(26, 14, "USB", 0x46DC87, text_font);
        add_label(28, 72, "HUB", 0xEB5F5F, text_font);

        knob_ = add_chip(32, 54, 16, 10, 0x46DC87, 0x2A6F49, 3, 1);
        if (knob_) {
            lv_anim_t animation;
            lv_anim_init(&animation);
            lv_anim_set_var(&animation, knob_);
            lv_anim_set_values(&animation, 54, 34);
            lv_anim_set_time(&animation, 650);
            lv_anim_set_playback_time(&animation, 650);
            lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
            lv_anim_set_exec_cb(
                &animation,
                [](void *object, int32_t value) {
                    if (object) lv_obj_set_y(static_cast<lv_obj_t *>(object), value);
                });
            lv_anim_start(&animation);
        }

        add_label(8, 80, "1  Slide LEFT switch  HUB -> USB", 0xECECEC, text_font);
        add_label(8, 95, "2  USB hub & peripherals turn OFF", 0xF0C850, text_font);
        add_label(8, 110, "3  Cable -> top-right USB-C port", 0x46DC87, text_font);
        confirm_label_ = add_label(8, metric(LayoutMetric::ScreenH) - 16,
                                   "OK: reboot now     ESC: later", 0x9A9AA0, text_font);
    }

private:
    static lv_obj_t *create_chip(lv_obj_t *parent,
                                 int pos_x,
                                 int pos_y,
                                 int width,
                                 int height,
                                 uint32_t background,
                                 uint32_t border,
                                 int radius,
                                 int border_width)
    {
        if (!parent) return nullptr;
        lv_obj_t *chip = lv_obj_create(parent);
        if (!chip) return nullptr;
        lv_obj_set_pos(chip, pos_x, pos_y);
        lv_obj_set_size(chip, width, height);
        lv_obj_set_style_radius(chip, radius, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chip, lv_color_hex(background), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(chip, lv_color_hex(border), LV_PART_MAIN);
        lv_obj_set_style_border_width(chip, border_width, LV_PART_MAIN);
        lv_obj_set_style_pad_all(chip, 0, LV_PART_MAIN);
        lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        return chip;
    }

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

    lv_obj_t *add_chip(int pos_x,
                       int pos_y,
                       int width,
                       int height,
                       uint32_t background,
                       uint32_t border,
                       int radius,
                       int border_width)
    {
        return create_chip(ComponensObj, pos_x, pos_y, width, height, background, border,
                           radius, border_width);
    }

    lv_obj_t *add_label(int pos_x,
                        int pos_y,
                        const char *text,
                        uint32_t color,
                        const lv_font_t *font)
    {
        return create_label(ComponensObj, pos_x, pos_y, text, color, font);
    }

    void stop_animation()
    {
        if (!knob_) return;
        lv_anim_del(knob_, nullptr);
        knob_ = nullptr;
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            stop_animation();
            if (LeaveSelfPage) LeaveSelfPage();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            stop_animation();
            if (page_node_->Componens_api)
                page_node_->Componens_api(SettingApiActivate, this);
            if (confirm_label_)
                lv_label_set_text(confirm_label_, "Reboot requested     ESC: back");
        }
        lv_event_stop_processing(event);
    }

    NodeIter page_node_;
    lv_obj_t *knob_ = nullptr;
    lv_obj_t *confirm_label_ = nullptr;
};
