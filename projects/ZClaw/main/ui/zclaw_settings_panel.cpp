#include "zclaw_settings_panel.h"

#include "zclaw_fonts.hpp"
#include "zclaw_theme.h"
#include "zclaw_widgets.h"

namespace zclaw {
namespace {

constexpr lv_coord_t kScreenWidth = 320;
constexpr lv_coord_t kScreenHeight = 170;
constexpr lv_coord_t kHeaderHeight = 20;

lv_coord_t centered_y(lv_coord_t container_height, lv_coord_t item_height)
{
    return (container_height - item_height) / 2;
}
}  // namespace

SettingsPanel::~SettingsPanel()
{
    destroy_panel();
}

bool SettingsPanel::create(lv_obj_t *parent, const FontManager *fonts)
{
    if (!parent || !fonts || !lifecycle_.mount())
        return false;
    fonts_ = fonts;
    panel_ = widgets::box(parent, kScreenWidth, 0, kScreenWidth, kScreenHeight,
                          theme::kBackground);
    lv_obj_add_event_cb(panel_, panel_deleted, LV_EVENT_DELETE, this);
    lv_obj_move_foreground(panel_);

    lv_obj_t *bar = widgets::box(panel_, 0, 0, kScreenWidth, kHeaderHeight,
                                 theme::kBar);
    const lv_coord_t title_height =
        lv_font_get_line_height(fonts_->font_12());
    const lv_coord_t hint_height =
        lv_font_get_line_height(fonts_->font_10());
    header_label_ = widgets::label(
        bar, "ZClaw Settings", 12, centered_y(kHeaderHeight, title_height),
        160, title_height,
                                   fonts_->font_12(), theme::kText);
    hint_label_ = widgets::label(bar, "Tab / Esc", 214,
                                 centered_y(kHeaderHeight, hint_height),
                                 94, hint_height,
                                 fonts_->font_10(), theme::kDim, LV_TEXT_ALIGN_RIGHT);
    widgets::box(bar, 0, kHeaderHeight - 1, kScreenWidth, 1, theme::kPanelLine);
    rows_container_ = widgets::box(panel_, 0, kHeaderHeight, kScreenWidth,
                                   kScreenHeight - kHeaderHeight,
                                   theme::kBackground);
    lv_obj_add_flag(rows_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rows_container_, static_cast<lv_obj_flag_t>(
        LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
        LV_OBJ_FLAG_SCROLL_CHAIN));
    lv_obj_set_scroll_dir(rows_container_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(rows_container_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(rows_container_, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(rows_container_, lv_color_hex(theme::kPurple),
                              LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(rows_container_, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(rows_container_, 1, LV_PART_SCROLLBAR);
    return true;
}

void SettingsPanel::show()
{
    if (panel_ && lifecycle_.begin_open())
        animate(kScreenWidth, 0);
}

void SettingsPanel::close()
{
    if (panel_ && lifecycle_.begin_close())
        animate(lv_obj_get_x(panel_), kScreenWidth);
}

bool SettingsPanel::is_open() const
{
    return lifecycle_.mounted();
}

bool SettingsPanel::is_animating() const
{
    return lifecycle_.animating();
}

lv_obj_t *SettingsPanel::content() const
{
    return panel_;
}

void SettingsPanel::set_header(const std::string &title, const std::string &hint)
{
    if (header_label_)
        lv_label_set_text(header_label_, title.c_str());
    if (hint_label_)
        lv_label_set_text(hint_label_, hint.c_str());
}

void SettingsPanel::clear_rows()
{
    for (lv_obj_t *&row : rows_) {
        if (row)
            lv_obj_del(row);
        row = nullptr;
    }
    for (lv_obj_t *&value : values_)
        value = nullptr;
    row_count_ = 0;
    if (rows_container_)
        lv_obj_scroll_to_y(rows_container_, 0, LV_ANIM_OFF);
}

bool SettingsPanel::add_row(const std::string &title, const std::string &value)
{
    if (!rows_container_ || !fonts_ || row_count_ >= kMaximumRows)
        return false;
    const int index = row_count_;
    const lv_coord_t y = 8 + index * 28;
    constexpr lv_coord_t row_height = 26;
    lv_obj_t *row = widgets::box(rows_container_, 12, y, 296, row_height,
                                 theme::kPanel, 8);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(row, lv_color_hex(theme::kPanelLine),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    const lv_coord_t text_height = lv_font_get_line_height(fonts_->font_10());
    const lv_coord_t text_y = centered_y(row_height, text_height);
    widgets::label(row, title, 10, text_y, 160, text_height,
                   fonts_->font_10(), theme::kText);
    values_[index] = widgets::label(row, value, 168, text_y, 118, text_height,
                                    fonts_->font_10(), theme::kMuted,
                                    LV_TEXT_ALIGN_RIGHT);
    widgets::box(row, 8, row_height - 3, 280, 1, theme::kPanelLine);
    rows_[index] = row;
    ++row_count_;
    return true;
}

int SettingsPanel::row_count() const
{
    return row_count_;
}

void SettingsPanel::update_selection(int selected_index)
{
    for (int index = 0; index < kMaximumRows; ++index) {
        if (!rows_[index])
            continue;
        const bool selected = index == selected_index;
        lv_obj_set_style_border_color(rows_[index],
                                      lv_color_hex(selected ? theme::kPurple
                                                            : theme::kPanelLine),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(rows_[index],
                                  lv_color_hex(selected ? theme::kSelectedPanel
                                                        : theme::kPanel),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        if (values_[index])
            lv_obj_set_style_text_color(values_[index],
                                        lv_color_hex(selected ? theme::kText : theme::kMuted),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (selected_index >= 0 && selected_index < kMaximumRows &&
        rows_[selected_index])
        lv_obj_scroll_to_view(rows_[selected_index], LV_ANIM_ON);
}

void SettingsPanel::animation_completed(lv_anim_t *animation)
{
    SettingsPanel *panel = static_cast<SettingsPanel *>(lv_anim_get_user_data(animation));
    if (panel)
        panel->on_animation_completed();
}

void SettingsPanel::on_animation_completed()
{
    if (lifecycle_.complete_animation())
        destroy_panel();
}

void SettingsPanel::animate(lv_coord_t from_x, lv_coord_t to_x)
{
    lv_anim_del(panel_, nullptr);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, panel_);
    lv_anim_set_exec_cb(&animation, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_x));
    lv_anim_set_values(&animation, from_x, to_x);
    lv_anim_set_time(&animation, 200);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, animation_completed);
    lv_anim_set_user_data(&animation, this);
    lv_anim_start(&animation);
}

void SettingsPanel::panel_deleted(lv_event_t *event)
{
    SettingsPanel *panel =
        static_cast<SettingsPanel *>(lv_event_get_user_data(event));
    if (panel)
        panel->release_panel();
}

void SettingsPanel::destroy_panel()
{
    lv_obj_t *panel = panel_;
    if (panel) {
        lv_anim_del(panel, nullptr);
        lv_obj_del(panel);
    }
    release_panel();
}

void SettingsPanel::release_panel()
{
    lifecycle_.release();
    fonts_ = nullptr;
    panel_ = nullptr;
    header_label_ = nullptr;
    hint_label_ = nullptr;
    rows_container_ = nullptr;
    for (lv_obj_t *&row : rows_)
        row = nullptr;
    for (lv_obj_t *&value : values_)
        value = nullptr;
    row_count_ = 0;
}

}  // namespace zclaw
