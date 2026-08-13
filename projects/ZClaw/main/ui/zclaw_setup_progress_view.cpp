#include "zclaw_setup_progress_view.h"

#include "zclaw_fonts.hpp"
#include "zclaw_setup_progress_model.h"
#include "zclaw_theme.h"
#include "zclaw_widgets.h"

namespace zclaw {
namespace {

constexpr uint32_t kScrollPixelsPerSecond = 20;

void set_scrolling_text(lv_obj_t *label, const std::string &text)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    const int32_t letter_space =
        lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
    const int32_t line_space =
        lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    lv_point_t text_size{};
    lv_point_t gap_size{};
    lv_text_get_size(&text_size, text.c_str(), font, letter_space, line_space,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&gap_size, "   ", font, letter_space, line_space,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const uint32_t duration = lv_anim_speed_to_time(
        kScrollPixelsPerSecond, 0, text_size.x + gap_size.x);
    lv_obj_set_style_anim_duration(label, duration, LV_PART_MAIN);
    lv_label_set_text(label, text.c_str());
}

}  // namespace

SetupProgressView::~SetupProgressView()
{
    clear();
}

void SetupProgressView::show(lv_obj_t *parent, const FontManager *fonts)
{
    if (!parent || !fonts)
        return;
    clear();
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(root_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                        LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_event_cb(root_, root_deleted, LV_EVENT_DELETE, this);

    spinner_ = lv_spinner_create(root_);
    lv_obj_set_size(spinner_, 22, 22);
    lv_obj_set_pos(spinner_, 18, 29);
    lv_spinner_set_anim_params(spinner_, 900, 70);
    lv_obj_set_style_arc_width(spinner_, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_, lv_color_hex(theme::kPanelLine), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner_, lv_color_hex(theme::kPurple), LV_PART_INDICATOR);

    const lv_coord_t title_height = lv_font_get_line_height(fonts->font_12());
    const lv_coord_t text_height = lv_font_get_line_height(fonts->font_10());
    status_label_ = widgets::label(root_, "Preparing Quickstart", 50, 26,
                                   252, title_height,
                                   fonts->font_12(), theme::kText);
    detail_label_ = widgets::label(root_, "Starting...", 50, 45, 252, text_height,
                                   fonts->font_10(), theme::kMuted);
    url_prefix_label_ = widgets::label(root_, "URL :", 18, 64, 38, text_height,
                                       fonts->font_10(), theme::kMuted);
    url_label_ = widgets::label(root_, "", 56, 64, 246, text_height,
                                fonts->font_10(), theme::kMuted);
    path_prefix_label_ = widgets::label(root_, "SAVE :", 18, 82, 38, text_height,
                                        fonts->font_10(), theme::kMuted);
    path_label_ = widgets::label(root_, "", 56, 82, 246, text_height,
                                 fonts->font_10(), theme::kMuted);
    lv_label_set_long_mode(url_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(path_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_add_flag(url_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(path_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(url_prefix_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(path_prefix_label_, LV_OBJ_FLAG_HIDDEN);
    progress_bar_ = lv_bar_create(root_);
    lv_obj_set_pos(progress_bar_, 18, 104);
    lv_obj_set_size(progress_bar_, 284, 10);
    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(progress_bar_, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(theme::kPanel), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(theme::kPurple), LV_PART_INDICATOR);
    percent_label_ = widgets::label(root_, "0%", 18, 117, 70, text_height,
                                    fonts->font_10(), theme::kText);
    speed_label_ = widgets::label(root_, "", 92, 117, 210, text_height,
                                  fonts->font_10(), theme::kMuted, LV_TEXT_ALIGN_RIGHT);
}

void SetupProgressView::update(const SetupProgress &progress)
{
    if (!is_visible())
        return;
    const SetupProgressPresentation presentation = present_setup_progress(progress);
    lv_label_set_text(status_label_, presentation.status.c_str());
    lv_label_set_text(detail_label_, presentation.detail.c_str());
    set_scrolling_text(url_label_, presentation.source_url);
    set_scrolling_text(path_label_, presentation.destination_path);
    if (presentation.source_url.empty()) {
        lv_obj_add_flag(url_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(url_prefix_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(url_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(url_prefix_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (presentation.destination_path.empty()) {
        lv_obj_add_flag(path_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(path_prefix_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(path_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(path_prefix_label_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_bar_set_value(progress_bar_, presentation.percent, LV_ANIM_ON);
    lv_label_set_text(percent_label_, presentation.percent_text.c_str());
    lv_label_set_text(speed_label_, presentation.speed.c_str());
}

void SetupProgressView::clear()
{
    if (root_)
        lv_obj_del(root_);
    release();
}

void SetupProgressView::root_deleted(lv_event_t *event)
{
    SetupProgressView *view =
        static_cast<SetupProgressView *>(lv_event_get_user_data(event));
    if (view)
        view->release();
}

void SetupProgressView::release()
{
    root_ = nullptr;
    spinner_ = nullptr;
    status_label_ = nullptr;
    detail_label_ = nullptr;
    url_prefix_label_ = nullptr;
    url_label_ = nullptr;
    path_prefix_label_ = nullptr;
    path_label_ = nullptr;
    progress_bar_ = nullptr;
    percent_label_ = nullptr;
    speed_label_ = nullptr;
}

bool SetupProgressView::is_visible() const
{
    return root_ != nullptr && progress_bar_ != nullptr;
}

}  // namespace zclaw
