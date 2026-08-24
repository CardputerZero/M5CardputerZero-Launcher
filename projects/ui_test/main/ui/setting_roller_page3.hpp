#pragma once

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iterator>
#include <list>
#include <string>
#include <utility>

#include "cp0_font_service.hpp"
#include "setting_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_componens.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

extern "C" {
extern const lv_image_dsc_t setting_red_up;
extern const lv_image_dsc_t setting_red_down;
extern const lv_image_dsc_t setting_right_arrow;
}

class LvSettingValuePage3Base : public DComponens::LvglComponensBase {
public:
    static constexpr int SCREEN_W          = 320;
    static constexpr int SCREEN_H          = 150;
    static constexpr int ROW_H             = 21;
    static constexpr int CENTER_ROW        = 3;
    static constexpr int EDGE_PADDING      = ROW_H * CENTER_ROW;
    static constexpr int BAR_X             = 4;
    static constexpr int BAR_Y             = 66;
    static constexpr int BAR_W             = 312;
    static constexpr int BAR_H             = 22;
    static constexpr int TITLE_CENTER_X    = 60;
    static constexpr int TITLE_BOX_W       = 84;
    static constexpr int VALUE_LIST_X      = 100;
    static constexpr int VALUE_LIST_W      = 120;
    static constexpr int VALUE_CENTER_X    = VALUE_LIST_W / 2;
    static constexpr int VALUE_BOX_X       = 16;
    static constexpr int VALUE_BOX_W       = 88;
    static constexpr int RIGHT_ARROW_SCALE = 224;

    int32_t selected_index = 0;
    std::function<void()> on_back = nullptr;

    LvSettingValuePage3Base() = default;

    ~LvSettingValuePage3Base() override
    {
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
        selection_bg_ = nullptr;
        value_list_   = nullptr;
        title_label_  = nullptr;
        right_arrow_  = nullptr;
        arrow_up_     = nullptr;
        arrow_down_   = nullptr;
        hint_         = nullptr;
    }

    static void style_value_label(lv_obj_t *label, int distance)
    {
        if (!label) return;

        int font_size  = 10;
        int opa        = 130;
        uint32_t color = 0x555555;
        if (distance == 0) {
            font_size = 16;
            opa       = 255;
            color     = 0xFFFFFF;
        } else if (distance == 1) {
            font_size = 12;
            opa       = 220;
            color     = 0xAAAAAA;
        } else if (distance == 2) {
            font_size = 12;
            opa       = 170;
            color     = 0x777777;
        }

        lv_obj_set_style_text_font(
            label,
            cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD),
            LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_opa(label, opa, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(label);

        const bool focused      = distance == 0;
        const int natural_width = lv_obj_get_width(label);
        if (natural_width > VALUE_BOX_W) {
            lv_obj_set_width(label, VALUE_BOX_W);
            lv_label_set_long_mode(
                label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, VALUE_BOX_X);
        } else {
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_update_layout(label);
            lv_obj_set_x(label, VALUE_CENTER_X - lv_obj_get_width(label) / 2);
        }

        const int label_y = (ROW_H - lv_obj_get_height(label)) / 2;
        lv_obj_set_y(label, std::max(0, label_y));
    }

    void update_title_position()
    {
        if (!title_label_) return;

        lv_obj_set_width(title_label_, LV_SIZE_CONTENT);
        lv_label_set_long_mode(title_label_, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(title_label_);

        if (lv_obj_get_width(title_label_) > TITLE_BOX_W) {
            lv_obj_set_width(title_label_, TITLE_BOX_W);
            lv_label_set_long_mode(title_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_x(title_label_, TITLE_CENTER_X - TITLE_BOX_W / 2);
        } else {
            lv_obj_set_x(title_label_,
                         TITLE_CENTER_X - lv_obj_get_width(title_label_) / 2);
        }

        const int label_y = BAR_Y + (BAR_H - lv_obj_get_height(title_label_)) / 2;
        lv_obj_set_y(title_label_, std::max(0, label_y));
    }

    void update_value_styles()
    {
        int index = 0;
        for (lv_obj_t *row : value_rows_) {
            if (!row) {
                ++index;
                continue;
            }
            lv_obj_t *label = lv_obj_get_child(row, 0);
            style_value_label(label, std::abs(index - selected_index));
            ++index;
        }
        update_right_arrow_position();
    }

    void update_right_arrow_position()
    {
        if (!right_arrow_ || !title_label_ || item_count_ == 0) return;

        lv_obj_t *row = row_at(selected_index);
        lv_obj_t *label = row ? lv_obj_get_child(row, 0) : nullptr;
        if (!label) return;

        lv_obj_update_layout(title_label_);
        lv_obj_update_layout(label);
        lv_obj_update_layout(right_arrow_);

        const int title_right = lv_obj_get_x(title_label_) + lv_obj_get_width(title_label_);
        const int value_left = VALUE_LIST_X + lv_obj_get_x(label);
        const int arrow_width = lv_obj_get_width(right_arrow_);
        const int arrow_x = std::max(title_right + 4, value_left - 4 - arrow_width);
        const int arrow_y = BAR_Y + (BAR_H - lv_obj_get_height(right_arrow_)) / 2;
        lv_obj_set_pos(right_arrow_, arrow_x, std::max(0, arrow_y));
        lv_obj_move_to_index(right_arrow_, 1);
    }

    void update_arrow_visibility()
    {
        if (arrow_up_) {
            if (selected_index > 0)
                lv_obj_remove_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
        }
        if (arrow_down_) {
            if (selected_index + 1 < static_cast<int32_t>(item_count_))
                lv_obj_remove_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void scroll_to_selected(bool animated)
    {
        if (!value_list_ || item_count_ == 0) return;

        lv_obj_t *row = row_at(selected_index);
        if (!row) return;

        lv_obj_scroll_to_view(row, animated ? LV_ANIM_ON : LV_ANIM_OFF);
        update_value_styles();
        update_arrow_visibility();
    }

    void select(int index)
    {
        if (item_count_ == 0) return;
        selected_index = std::clamp(index, 0, static_cast<int>(item_count_ - 1));
        scroll_to_selected(false);
    }

    lv_obj_t *row_at(int index) const
    {
        if (index < 0 || index >= static_cast<int>(value_rows_.size())) return nullptr;
        auto row = std::next(value_rows_.begin(), index);
        return row == value_rows_.end() ? nullptr : *row;
    }

protected:
    LvSettingValuePage3Base(const NodeIter &parent_node,
                            std::function<void()> back_callback)
        : parent_node_(parent_node), on_back(std::move(back_callback))
    {
    }

    void initialize(lv_obj_t *parent)
    {
        create_ui(parent);
    }

    const NodeIter &parent_node() const
    {
        return parent_node_;
    }

    virtual int initial_selection() const = 0;

    virtual void activate_selected()
    {
        if (item_count_ == 0) return;

        auto selected_node = std::next(parent_node_.begin(), selected_index);
        if (selected_node->Componens_api)
            selected_node->Componens_api(SettingApiActivate, this);
        if (on_back) on_back();
    }

    void key_event_cb(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (on_back) on_back();
            lv_event_stop_processing(event);
            return;
        }

        if (key == LV_KEY_UP) {
            if (selected_index > 0) {
                --selected_index;
                scroll_to_selected(true);
            }
        } else if (key == LV_KEY_DOWN) {
            if (selected_index + 1 < static_cast<int32_t>(item_count_)) {
                ++selected_index;
                scroll_to_selected(true);
            }
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            activate_selected();
        }

        lv_event_stop_processing(event);
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;

        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj, SCREEN_W, SCREEN_H);
        lv_obj_set_pos(ComponensObj, 0, 0);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
        DComponens::lvgl_bind_event(
            ComponensObj,
            LV_EVENT_KEY,
            nullptr,
            std::bind(&LvSettingValuePage3Base::key_event_cb, this, std::placeholders::_1));

        selection_bg_ = lv_obj_create(ComponensObj);
        if (selection_bg_) {
            lv_obj_set_size(selection_bg_, BAR_W, BAR_H);
            lv_obj_set_pos(selection_bg_, BAR_X, BAR_Y);
            lv_obj_set_style_bg_color(selection_bg_, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(selection_bg_, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(selection_bg_, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(selection_bg_, 0, LV_PART_MAIN);
            lv_obj_remove_flag(selection_bg_, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(selection_bg_, LV_OBJ_FLAG_SCROLLABLE);
        }

        value_list_ = lv_obj_create(ComponensObj);
        if (value_list_) {
            lv_obj_set_size(value_list_, VALUE_LIST_W, SCREEN_H);
            lv_obj_set_pos(value_list_, VALUE_LIST_X, 0);
            lv_obj_set_style_bg_opa(value_list_, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_top(value_list_, EDGE_PADDING, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(value_list_, EDGE_PADDING, LV_PART_MAIN);
            lv_obj_set_style_pad_row(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_clip_corner(value_list_, true, LV_PART_MAIN);
            lv_obj_set_flex_flow(value_list_, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_scroll_dir(value_list_, LV_DIR_VER);
            lv_obj_set_scroll_snap_y(value_list_, LV_SCROLL_SNAP_CENTER);
            lv_obj_set_scrollbar_mode(value_list_, LV_SCROLLBAR_MODE_OFF);
            lv_obj_remove_flag(value_list_, LV_OBJ_FLAG_SCROLL_WITH_ARROW);

            for (auto it = parent_node_.begin(); it != parent_node_.end(); ++it) {
                lv_obj_t *row = lv_obj_create(value_list_);
                if (!row) continue;
                value_rows_.push_back(row);
                lv_obj_set_size(row, VALUE_LIST_W, ROW_H);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                lv_obj_t *label = lv_label_create(row);
                if (label) lv_label_set_text(label, it->label.c_str());
            }
            item_count_ = static_cast<uint32_t>(value_rows_.size());
        }

        title_label_ = lv_label_create(ComponensObj);
        if (title_label_) {
            lv_label_set_text(title_label_, parent_node_->label.c_str());
            lv_obj_set_style_text_font(
                title_label_,
                cp0_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
                LV_PART_MAIN);
            lv_obj_set_style_text_color(title_label_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            update_title_position();
        }

        right_arrow_ = lv_img_create(ComponensObj);
        if (right_arrow_) {
            lv_img_set_src(right_arrow_, &setting_right_arrow);
            lv_image_set_pivot(right_arrow_, 0, 0);
            lv_image_set_scale(right_arrow_, RIGHT_ARROW_SCALE);
            lv_obj_update_layout(right_arrow_);
            lv_obj_set_pos(right_arrow_, VALUE_LIST_X - lv_obj_get_width(right_arrow_) - 4,
                           BAR_Y + (BAR_H - lv_obj_get_height(right_arrow_)) / 2);
        }

        arrow_up_ = lv_img_create(ComponensObj);
        if (arrow_up_) {
            lv_img_set_src(arrow_up_, &setting_red_up);
            lv_obj_update_layout(arrow_up_);
            lv_obj_set_pos(arrow_up_,
                           VALUE_LIST_X + VALUE_CENTER_X - lv_obj_get_width(arrow_up_) / 2,
                           2);
        }

        arrow_down_ = lv_img_create(ComponensObj);
        if (arrow_down_) {
            lv_img_set_src(arrow_down_, &setting_red_down);
            lv_obj_update_layout(arrow_down_);
            lv_obj_set_pos(arrow_down_,
                           VALUE_LIST_X + VALUE_CENTER_X - lv_obj_get_width(arrow_down_) / 2,
                           SCREEN_H - lv_obj_get_height(arrow_down_) - 4);
        }

        hint_ = lv_label_create(ComponensObj);
        if (hint_) {
            lv_label_set_text(hint_, "ok:set");
            lv_obj_set_style_text_color(hint_, lv_color_hex(0x00CC66), LV_PART_MAIN);
            lv_obj_set_style_text_font(
                hint_,
                cp0_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
                LV_PART_MAIN);
            lv_obj_update_layout(hint_);
            lv_obj_set_pos(hint_, SCREEN_W - 6 - lv_obj_get_width(hint_),
                           BAR_Y + (BAR_H - lv_obj_get_height(hint_)) / 2);
        }

        if (item_count_ > 0) {
            lv_obj_update_layout(value_list_);
            select(initial_selection());
        }
        else update_arrow_visibility();
    }

private:
    NodeIter parent_node_;
    uint32_t item_count_ = 0;
    std::list<lv_obj_t *> value_rows_;
    lv_obj_t *selection_bg_ = nullptr;
    lv_obj_t *value_list_   = nullptr;
    lv_obj_t *title_label_  = nullptr;
    lv_obj_t *right_arrow_  = nullptr;
    lv_obj_t *arrow_up_     = nullptr;
    lv_obj_t *arrow_down_   = nullptr;
    lv_obj_t *hint_         = nullptr;
};

class LvSettingRollerPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRollerPage3() = default;

    LvSettingRollerPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingRollerPage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

private:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingBrightnessPage3 : public LvSettingValuePage3Base {
public:
    LvSettingBrightnessPage3() = default;

    LvSettingBrightnessPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingBrightnessPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingDarkTimePage3 : public LvSettingValuePage3Base {
public:
    LvSettingDarkTimePage3() = default;

    LvSettingDarkTimePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingDarkTimePage3(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 2;
    }
};

class LvSettingVolumePage3 : public LvSettingValuePage3Base {
public:
    LvSettingVolumePage3() = default;

    LvSettingVolumePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingVolumePage3(lv_obj_t *parent,
                         const NodeIter &parent_node,
                         std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingResolutionPage3 : public LvSettingValuePage3Base {
public:
    LvSettingResolutionPage3() = default;

    LvSettingResolutionPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingResolutionPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingBQCalibratePage3 : public LvSettingValuePage3Base {
public:
    LvSettingBQCalibratePage3() = default;

    LvSettingBQCalibratePage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingBQCalibratePage3(lv_obj_t *parent,
                              const NodeIter &parent_node,
                              std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 0;
    }
};

class LvSettingRtcPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRtcPage3() = default;

    LvSettingRtcPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingRtcPage3(lv_obj_t *parent,
                      const NodeIter &parent_node,
                      std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        const std::string &label = parent_node()->label;
        const std::time_t now = std::time(nullptr);
        const std::tm *local = std::localtime(&now);
        if (!local) return 0;
        if (label == "Year") return local->tm_year + 1900 - 2000;
        if (label == "Month") return local->tm_mon;
        if (label == "Day") return local->tm_mday - 1;
        if (label == "Hour") return local->tm_hour;
        if (label == "Minute") return local->tm_min;
        if (label == "Second") return local->tm_sec;
        return 0;
    }
};

class LvSettingConfirmPage3 : public LvSettingValuePage3Base {
public:
    LvSettingConfirmPage3() = default;

    LvSettingConfirmPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingConfirmPage3(lv_obj_t *parent,
                          const NodeIter &parent_node,
                          std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        return 1;
    }
};
