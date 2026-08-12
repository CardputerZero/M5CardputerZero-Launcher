#include "zclaw_startup_view.h"

#include "zclaw_fonts.hpp"
#include "zclaw_theme.h"
#include "zclaw_widgets.h"

namespace zclaw {
namespace {

constexpr lv_coord_t kScreenWidth = 320;
constexpr lv_coord_t kScreenHeight = 170;

}  // namespace

StartupView::~StartupView()
{
    destroy_overlay();
}

void StartupView::show_network_check(lv_obj_t *parent, const FontManager *fonts)
{
    if (!parent || !fonts)
        return;
    destroy_overlay();
    fonts_ = fonts;
    state_ = StartupState::CheckingNetwork;
    overlay_ = widgets::box(parent, 0, 0, kScreenWidth, kScreenHeight, theme::kBackground);
    lv_obj_add_event_cb(overlay_, overlay_deleted, LV_EVENT_DELETE, this);
    lv_obj_move_foreground(overlay_);
    const lv_coord_t title_height = lv_font_get_line_height(fonts_->font_12());
    const lv_coord_t text_height = lv_font_get_line_height(fonts_->font_10());
    widgets::label(overlay_, "ZCLAW", 16, 24, 288, title_height,
                   fonts_->font_12(), theme::kPurple,
                   LV_TEXT_ALIGN_CENTER);
    title_ = widgets::label(overlay_, "Checking network...", 16, 68, 288,
                            title_height,
                            fonts_->font_12(), theme::kText, LV_TEXT_ALIGN_CENTER);
    detail_ = widgets::label(overlay_, "Please wait", 16, 96, 288, text_height,
                             fonts_->font_10(), theme::kMuted, LV_TEXT_ALIGN_CENTER);
}

void StartupView::show_offline(const std::string &error)
{
    if (!overlay_ || !title_ || !detail_ || !fonts_)
        return;
    state_ = StartupState::Offline;
    lv_label_set_text(title_, "No internet connection");
    const std::string detail = error.empty() ? "Check Wi-Fi and try again." :
                               "Check Wi-Fi and try again.\n" + error;
    lv_label_set_text(detail_, detail.c_str());
    const lv_coord_t text_height = lv_font_get_line_height(fonts_->font_10());
    lv_obj_set_height(detail_, text_height * 2);
    if (!action_)
        action_ = widgets::label(overlay_, "OK  EXIT", 16, 140, 288,
                                 text_height,
                                 fonts_->font_10(), theme::kPurple, LV_TEXT_ALIGN_CENTER);
}

void StartupView::show_ready()
{
    state_ = StartupState::Ready;
    destroy_overlay();
}

StartupState StartupView::state() const
{
    return state_;
}

void StartupView::destroy_overlay()
{
    lv_obj_t *overlay = overlay_;
    if (overlay)
        lv_obj_del(overlay);
    release_overlay();
}

void StartupView::overlay_deleted(lv_event_t *event)
{
    StartupView *view =
        static_cast<StartupView *>(lv_event_get_user_data(event));
    if (view)
        view->release_overlay();
}

void StartupView::release_overlay()
{
    fonts_ = nullptr;
    overlay_ = nullptr;
    title_ = nullptr;
    detail_ = nullptr;
    action_ = nullptr;
}

}  // namespace zclaw
