#include "zclaw_shell_view.h"

#include "zclaw_fonts.hpp"
#include "zclaw_theme.h"
#include "zclaw_widgets.h"

namespace zclaw {
namespace {

constexpr lv_coord_t kScreenWidth = 320;
constexpr lv_coord_t kScreenHeight = 170;
constexpr lv_coord_t kTopBarNameX = 47;
constexpr lv_coord_t kTopBarLabelGap = 4;
lv_coord_t centered_y(lv_coord_t container_height, lv_coord_t item_height)
{
    return (container_height - item_height) / 2;
}

lv_coord_t text_width(const char *text, const lv_font_t *font)
{
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    return size.x;
}

}  // namespace

ShellView::~ShellView()
{
    destroy_root();
}

bool ShellView::create(lv_obj_t *parent, const FontManager *fonts,
                       const std::string &avatar_path,
                       const std::string &sparkles_path,
                       const std::string &send_button_path)
{
    if (root_ || !parent || !fonts)
        return false;
    root_ = widgets::box(parent, 0, 0, kScreenWidth, kScreenHeight,
                         theme::kBackground);
    lv_obj_add_event_cb(root_, root_deleted, LV_EVENT_DELETE, this);
    lv_obj_move_foreground(root_);
    create_top_bar(fonts, avatar_path);
    create_input_bar(fonts, sparkles_path, send_button_path);
    return true;
}

lv_obj_t *ShellView::content() const
{
    return root_;
}

void ShellView::root_deleted(lv_event_t *event)
{
    ShellView *view = static_cast<ShellView *>(lv_event_get_user_data(event));
    if (view)
        view->release_root();
}

void ShellView::destroy_root()
{
    lv_obj_t *root = root_;
    if (root)
        lv_obj_del(root);
    release_root();
}

void ShellView::release_root()
{
    root_ = nullptr;
}

void ShellView::create_top_bar(const FontManager *fonts,
                               const std::string &avatar_path)
{
    constexpr lv_coord_t bar_height = 20;
    constexpr lv_coord_t avatar_size = 16;
    constexpr lv_coord_t status_size = 6;
    constexpr lv_coord_t dot_size = 2;
    constexpr char name[] = "ZClaw";
    constexpr char status[] = "Online";
    const lv_coord_t name_height = lv_font_get_line_height(fonts->font_12());
    const lv_coord_t status_height = lv_font_get_line_height(fonts->font_10());
    const lv_coord_t name_width = text_width(name, fonts->font_12());
    const lv_coord_t status_width = text_width(status, fonts->font_10());
    const lv_coord_t status_x =
        kTopBarNameX + name_width + kTopBarLabelGap;

    lv_obj_t *bar = widgets::box(root_, 0, 0, kScreenWidth, bar_height,
                                 theme::kBar);
    widgets::image(bar, avatar_path, 12, centered_y(bar_height, avatar_size));
    widgets::box(bar, 34, centered_y(bar_height, status_size), status_size,
                 status_size, theme::kOnline, status_size / 2);
    widgets::label(bar, name, kTopBarNameX,
                   centered_y(bar_height, name_height), name_width,
                   name_height, fonts->font_12(), theme::kText);
    widgets::label(bar, status, status_x,
                   centered_y(bar_height, status_height), status_width,
                   status_height, fonts->font_10(), theme::kOnline);

    const lv_coord_t ellipsis_y = centered_y(bar_height, dot_size);
    widgets::box(bar, 295, ellipsis_y, dot_size, dot_size, theme::kMuted,
                 dot_size / 2);
    widgets::box(bar, 300, ellipsis_y, dot_size, dot_size, theme::kMuted,
                 dot_size / 2);
    widgets::box(bar, 305, ellipsis_y, dot_size, dot_size, theme::kMuted,
                 dot_size / 2);
}

void ShellView::create_input_bar(const FontManager *fonts,
                                 const std::string &sparkles_path,
                                 const std::string &send_button_path)
{
    lv_obj_t *input_bar = widgets::box(root_, 0, 148, kScreenWidth, 22,
                                       theme::kBar);
    const lv_coord_t text_height = lv_font_get_line_height(fonts->font_10());
    widgets::box(input_bar, 0, 0, kScreenWidth, 1, theme::kPanelLine);
    lv_obj_t *input_box = widgets::box(input_bar, 10, 2, 274, 18,
                                       theme::kPanel, 8);
    widgets::image(input_box, sparkles_path, 8, 3);
    widgets::label(input_box, "Press Enter to ask", 26,
                   centered_y(18, text_height), 180, text_height,
                   fonts->font_10(), theme::kDim);
    widgets::image(input_bar, send_button_path, 292, 2);
}

}  // namespace zclaw
