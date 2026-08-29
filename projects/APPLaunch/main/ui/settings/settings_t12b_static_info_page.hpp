#pragma once

#include "cp0_font_service.hpp"
#include "settings_t12b_about_help_model.hpp"
#include "settings_tree_types.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "lvgl_components.hpp"

class LvSettingStaticInfoPage3 : public DComponens::LvglComponensBase {
public:
    LvSettingStaticInfoPage3() = default;

    LvSettingStaticInfoPage3(lv_obj_t *parent,
                             const NodeIter &page_node,
                             std::function<void()> back_callback,
                             settings_t12b::about_help::Content content)
        : page_node_(page_node), content_(std::move(content))
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
    }

    ~LvSettingStaticInfoPage3() override
    {
        if (ComponensObj) {
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
    }

    void AnimateNextIn(std::function<void()> callback) override
    {
        if (callback) callback();
    }

    void AnimateNextOut(std::function<void()> callback) override
    {
        if (callback) callback();
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

        lv_obj_set_size(ComponensObj, 320, 150);
        lv_obj_set_pos(ComponensObj, 0, 0);
        lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);

        add_label(content_.title, 8, 6, 0x58A6FF, &lv_font_montserrat_16, false);

        int y = 30;
        for (const std::string &line : content_.lines) {
            if (y >= 132) break;
            lv_obj_t *label = add_label(line, 8, y, 0xE0E0E0, &lv_font_montserrat_12, true);
            if (!label) break;
            lv_obj_update_layout(label);
            y += lv_obj_get_height(label) + 3;
        }

        const lv_font_t *hint_font = cp0_fonts().get(
            "Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD);
        add_label("ESC: back", 8, 133, 0x46DC87,
                  hint_font ? hint_font : &lv_font_montserrat_12, false);
        DComponens::lvgl_bind_event(
            ComponensObj, LV_EVENT_KEY, nullptr,
            [this](lv_event_t *event) { handle_key_event(event); });
    }

private:
    lv_obj_t *add_label(const std::string &text,
                        int x,
                        int y,
                        uint32_t color,
                        const lv_font_t *font,
                        bool wrap)
    {
        lv_obj_t *label = lv_label_create(ComponensObj);
        if (!label) return nullptr;
        lv_label_set_text(label, text.c_str());
        lv_obj_set_pos(label, x, y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        if (wrap) {
            lv_obj_set_width(label, 304);
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        }
        return label;
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (LeaveSelfPage) LeaveSelfPage();
            lv_event_stop_processing(event);
        }
    }

    NodeIter page_node_;
    settings_t12b::about_help::Content content_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_t12b_about_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> on_back);

std::unique_ptr<DComponens::LvglComponensBase> settings_t12b_help_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> on_back);
