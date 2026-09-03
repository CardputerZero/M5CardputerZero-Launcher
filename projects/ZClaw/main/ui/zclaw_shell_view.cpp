#include "zclaw_shell_view.h"

#include "zclaw_fonts.hpp"
#include "zclaw_theme.h"
#include "zclaw_widgets.h"

namespace zclaw {
namespace {

constexpr lv_coord_t kScreenWidth = 320;
constexpr lv_coord_t kScreenHeight = 170;

enum class XPosition : lv_coord_t {
    Origin = 0,
    Avatar = 12,
    Name = 34,
    InputBox = 10,
    Sparkles = 8,
    InputText = 26,
    SendButton = 292,
    SettingsHint = 282,
};

enum class YPosition : lv_coord_t {
    Origin = 0,
    InputBar = 148,
    InputBox = 2,
    Sparkles = 3,
    SendButton = 2,
};

constexpr lv_coord_t to_coord(XPosition position)
{
    return static_cast<lv_coord_t>(position);
}

constexpr lv_coord_t to_coord(YPosition position)
{
    return static_cast<lv_coord_t>(position);
}

lv_coord_t centered_y(lv_coord_t container_height, lv_coord_t item_height)
{
    return (container_height - item_height) / 2;
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
    root_ = widgets::box(parent, to_coord(XPosition::Origin),
                         to_coord(YPosition::Origin), kScreenWidth, kScreenHeight,
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
    const lv_coord_t name_height = lv_font_get_line_height(fonts->font_12());

    lv_obj_t *bar = widgets::box(root_, to_coord(XPosition::Origin),
                                 to_coord(YPosition::Origin), kScreenWidth,
                                 bar_height, theme::kBar);
    widgets::image(bar, avatar_path, to_coord(XPosition::Avatar),
                   centered_y(bar_height, avatar_size));
    widgets::label(bar, "ZClaw", to_coord(XPosition::Name),
                   centered_y(bar_height, name_height), 64,
                   name_height, fonts->font_12(), theme::kText);

    const lv_coord_t hint_height = lv_font_get_line_height(fonts->font_10());
    widgets::label(bar, "TAB", to_coord(XPosition::SettingsHint),
                   centered_y(bar_height, hint_height), 30, hint_height,
                   fonts->font_10(), theme::kMuted, LV_TEXT_ALIGN_RIGHT);
}

void ShellView::create_input_bar(const FontManager *fonts,
                                 const std::string &sparkles_path,
                                 const std::string &send_button_path)
{
    lv_obj_t *input_bar = widgets::box(root_, to_coord(XPosition::Origin),
                                       to_coord(YPosition::InputBar), kScreenWidth,
                                       22,
                                       theme::kBar);
    const lv_coord_t text_height = lv_font_get_line_height(fonts->font_10());
    widgets::box(input_bar, to_coord(XPosition::Origin),
                 to_coord(YPosition::Origin), kScreenWidth, 1,
                 theme::kPanelLine);
    lv_obj_t *input_box = widgets::box(input_bar, to_coord(XPosition::InputBox),
                                       to_coord(YPosition::InputBox), 274, 18,
                                       theme::kPanel, 8);
    widgets::image(input_box, sparkles_path, to_coord(XPosition::Sparkles),
                   to_coord(YPosition::Sparkles));
    widgets::label(input_box, "Press Enter to ask", to_coord(XPosition::InputText),
                   centered_y(18, text_height), 180, text_height,
                   fonts->font_10(), theme::kDim);
    widgets::image(input_bar, send_button_path, to_coord(XPosition::SendButton),
                   to_coord(YPosition::SendButton));
}

}  // namespace zclaw
