#pragma once

#include "setting_soundcard_detail_page.hpp"

class LvSettingSoundCardPage4 : public DComponens::LvglComponensBase {
public:
    LvSettingSoundCardPage4() = default;

    LvSettingSoundCardPage4(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback)
        : parent_node_(parent_node)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
        load_cards();
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

    ~LvSettingSoundCardPage4() override
    {
        lv_async_call_cancel(&LvSettingSoundCardPage4::close_detail_async, this);
        detail_close_pending_ = false;
        if (detail_page_ && detail_page_->Get()) {
            if (lv_obj_get_group(detail_page_->Get()))
                lv_group_remove_obj(detail_page_->Get());
        }
        detail_page_.reset();
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
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
            std::bind(&LvSettingSoundCardPage4::handle_key_event,
                      this,
                      std::placeholders::_1));
    }

private:
    enum class View { Cards, Controls };

    enum class LayoutMetric : int {
        ScreenW     = 320,
        ScreenH     = 150,
        RowY        = 34,
        RowH        = 20,
        RowGap      = 2,
        VisibleRows = 5,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    lv_obj_t *label(const char *text,
                    int x,
                    int y,
                    int width,
                    int height,
                    uint32_t color,
                    int font_size,
                    bool scroll = false)
    {
        lv_obj_t *result = lv_label_create(ComponensObj);
        if (!result) return nullptr;
        lv_label_set_text(result, text ? text : "");
        lv_obj_set_size(result, width, height);
        lv_obj_set_pos(result, x, y);
        lv_obj_set_style_text_color(result, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_text_font(
            result,
            cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD),
            LV_PART_MAIN);
        lv_obj_set_style_text_align(result, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_label_set_long_mode(result, scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
        return result;
    }

    void load_cards()
    {
        view_ = View::Cards;
        selected_index_ = 0;
        card_index_ = -1;
        controls_.clear();
        cards_ = ui_test_soundcard::mock_cards();
        backend_available_ = true;
        render();
    }

    void load_controls()
    {
        if (cards_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(cards_.size()))
            return;

        const int requested_card = cards_[selected_index_].index;
        controls_ = ui_test_soundcard::mock_controls(requested_card);
        card_index_ = requested_card;
        selected_index_ = 0;
        view_ = View::Controls;
        backend_available_ = true;
        render();
    }

    void open_detail()
    {
        if (controls_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(controls_.size()))
            return;

        ui_test_soundcard::Control control = controls_[selected_index_];

        lv_group_t *group = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr;
        detail_page_ = std::make_unique<LvSettingSoundCardDetailPage>(
            ComponensObj,
            card_index_,
            std::move(control),
            std::bind(&LvSettingSoundCardPage4::close_detail, this));
        if (!detail_page_ || !detail_page_->Get()) {
            detail_page_.reset();
            return;
        }
        if (group) {
            if (lv_obj_get_group(ComponensObj) == group)
                lv_group_remove_obj(ComponensObj);
            lv_group_add_obj(group, detail_page_->Get());
            lv_group_focus_obj(detail_page_->Get());
        }
    }

    static void close_detail_async(void *user_data)
    {
        auto *self = static_cast<LvSettingSoundCardPage4 *>(user_data);
        if (self) self->close_detail_now();
    }

    void close_detail()
    {
        if (!detail_page_ || detail_close_pending_) return;
        detail_close_pending_ = true;
        if (lv_async_call(&LvSettingSoundCardPage4::close_detail_async, this) != LV_RESULT_OK) {
            detail_close_pending_ = false;
            close_detail_now();
        }
    }

    void close_detail_now()
    {
        detail_close_pending_ = false;
        if (detail_page_ && selected_index_ >= 0 &&
            selected_index_ < static_cast<int>(controls_.size())) {
            const ui_test_soundcard::Control updated = detail_page_->control();
            if (updated.name == controls_[selected_index_].name)
                controls_[selected_index_] = updated;
        }
        lv_group_t *group = detail_page_ && detail_page_->Get()
                                ? lv_obj_get_group(detail_page_->Get())
                                : nullptr;
        if (detail_page_ && detail_page_->Get() && group)
            lv_group_remove_obj(detail_page_->Get());
        detail_page_.reset();
        if (group && ComponensObj) {
            lv_group_add_obj(group, ComponensObj);
            lv_group_focus_obj(ComponensObj);
        }
        render();
    }

    void render()
    {
        if (!ComponensObj || detail_page_) return;
        lv_obj_clean(ComponensObj);

        const bool controls_view = view_ == View::Controls;
        const char *title = controls_view ? "Mixer Controls" : "Sound Cards";
        label(title, 8, 5, 210, 20, 0x58A6FF, 14);

        if (!backend_available_) {
            label("Mixer unavailable", 8, 48, 304, 20, 0xFFAA00, 13);
            label("Soundcard backend is not supported", 8, 72, 304, 16, 0x888888, 11);
            label("ESC: back", 8, 126, 304, 14, 0x555555, 10);
            return;
        }

        const int count = controls_view ? static_cast<int>(controls_.size())
                                        : static_cast<int>(cards_.size());
        if (count == 0) {
            label(controls_view ? "No mixer controls" : "No ALSA cards found",
                  8, 54, 304, 20, 0x888888, 12);
            label("ESC: back", 8, 126, 304, 14, 0x555555, 10);
            return;
        }

        const int offset = std::clamp(selected_index_ - metric(LayoutMetric::VisibleRows) / 2,
                                      0,
                                      std::max(0, count - metric(LayoutMetric::VisibleRows)));
        for (int visible = 0;
             visible < metric(LayoutMetric::VisibleRows) && offset + visible < count;
             ++visible) {
            const int index = offset + visible;
            const bool focused = index == selected_index_;
            const int y = metric(LayoutMetric::RowY) +
                          visible * (metric(LayoutMetric::RowH) + metric(LayoutMetric::RowGap));
            lv_obj_t *row = lv_obj_create(ComponensObj);
            if (!row) continue;
            lv_obj_set_size(row, 304, metric(LayoutMetric::RowH));
            lv_obj_set_pos(row, 8, y);
            lv_obj_set_style_bg_color(row, lv_color_hex(focused ? 0x2A2A2A : 0x000000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row, focused ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            std::string name;
            std::string value;
            if (controls_view) {
                name = controls_[index].name;
                value = controls_[index].current_text;
            } else {
                name = cards_[index].label;
                value = "card " + std::to_string(cards_[index].index);
            }
            lv_obj_t *name_label = lv_label_create(row);
            if (name_label) {
                lv_label_set_text(name_label, name.c_str());
                lv_obj_set_size(name_label,
                                controls_view ? 175 : 230,
                                metric(LayoutMetric::RowH));
                lv_obj_set_pos(name_label, 4, 0);
                lv_obj_set_style_text_color(name_label, lv_color_hex(focused ? 0xFFFFFF : 0xCCCCCC), LV_PART_MAIN);
                lv_obj_set_style_text_font(name_label, cp0_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
                lv_label_set_long_mode(name_label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            }
            if (controls_view && !value.empty()) {
                lv_obj_t *value_label = lv_label_create(row);
                if (value_label) {
                    lv_label_set_text(value_label, value.c_str());
                    lv_obj_set_size(value_label, 112, metric(LayoutMetric::RowH));
                    lv_obj_set_pos(value_label, 184, 0);
                    lv_obj_set_style_text_color(value_label, lv_color_hex(focused ? 0xAADDFF : 0x777777), LV_PART_MAIN);
                    lv_obj_set_style_text_font(value_label, cp0_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
                    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
                }
            }
        }

        if (selected_index_ > 0)
            add_arrow(0, "up");
        if (selected_index_ + 1 < count)
            add_arrow(metric(LayoutMetric::ScreenH) - 19, "down");
        label(controls_view ? "OK: edit  ESC: back" : "OK: open  ESC: back",
              8, 126, 304, 14, 0x555555, 10);
    }

    void add_arrow(int y, const char *direction)
    {
        lv_obj_t *arrow = lv_img_create(ComponensObj);
        if (!arrow) return;
        lv_img_set_src(arrow, std::string(direction) == "up" ? &setting_red_up : &setting_red_down);
        lv_obj_update_layout(arrow);
        lv_obj_set_pos(arrow,
                       metric(LayoutMetric::ScreenW) / 2 - lv_obj_get_width(arrow) / 2,
                       y);
    }

    void handle_key(uint32_t key)
    {
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (view_ == View::Controls) {
                load_cards();
            } else if (LeaveSelfPage) {
                LeaveSelfPage();
            }
            return;
        }

        const int count = view_ == View::Controls ? static_cast<int>(controls_.size())
                                                  : static_cast<int>(cards_.size());
        if (count == 0) return;
        if (key == LV_KEY_UP) {
            selected_index_ = selected_index_ > 0 ? selected_index_ - 1 : count - 1;
            render();
        } else if (key == LV_KEY_DOWN) {
            selected_index_ = selected_index_ + 1 < count ? selected_index_ + 1 : 0;
            render();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            if (view_ == View::Cards)
                load_controls();
            else
                open_detail();
        }
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        handle_key(lv_event_get_key(event));
        lv_event_stop_processing(event);
    }

    NodeIter parent_node_;
    View view_ = View::Cards;
    std::vector<ui_test_soundcard::Card> cards_;
    std::vector<ui_test_soundcard::Control> controls_;
    int selected_index_ = 0;
    int card_index_ = -1;
    bool backend_available_ = false;
    bool detail_close_pending_ = false;
    std::unique_ptr<LvSettingSoundCardDetailPage> detail_page_;
};

inline std::unique_ptr<DComponens::LvglComponensBase> soundcard_page4_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back)
{
    return std::make_unique<LvSettingSoundCardPage4>(
        parent, page_node, std::move(on_back));
}
