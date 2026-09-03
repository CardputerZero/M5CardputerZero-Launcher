#include "zclaw_chat_view.h"

#include "zclaw_fonts.hpp"
#include "zclaw_chat_layout.h"
#include "zclaw_theme.h"
#include "zclaw_widgets.h"

#include <utility>

namespace zclaw {
namespace {

constexpr lv_coord_t kChatRowWidth = 296;
constexpr lv_coord_t kUserBubblePadX = 10;
constexpr lv_coord_t kUserBubblePadY = 6;
constexpr lv_coord_t kTrackY = 20;
constexpr lv_coord_t kTrackHeight = 126;

lv_point_t measure_text(const std::string &text, const lv_font_t *font, lv_coord_t max_width)
{
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, max_width, LV_TEXT_FLAG_NONE);
    return size;
}

}  // namespace

ChatView::~ChatView()
{
    release();
}

void ChatView::create(lv_obj_t *parent, const FontManager *fonts, std::string avatar_path)
{
    if (container_ || !parent || !fonts)
        return;
    fonts_ = fonts;
    avatar_path_ = std::move(avatar_path);
    widgets::box(parent, 0, 20, 320, 128, theme::kBackground);

    container_ = lv_obj_create(parent);
    lv_obj_set_pos(container_, 0, 20);
    lv_obj_set_size(container_, 312, 128);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(container_, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(container_, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(container_, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(container_, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(container_, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(container_, LV_DIR_VER);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(container_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_ELASTIC |
                                                             LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                                             LV_OBJ_FLAG_SCROLL_CHAIN));
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(container_, scroll_event, LV_EVENT_SCROLL, this);
    lv_obj_add_event_cb(container_, container_deleted, LV_EVENT_DELETE, this);

    append_assistant_message("Hello! I am ZClaw.\nReady to help with your device.");

    scroll_track_ = widgets::box(parent, 312, kTrackY, 3, kTrackHeight, 0x33335A, 2);
    scroll_thumb_ = widgets::box(parent, 312, 28, 3, 38, 0x818CF8, 2);
    lv_obj_set_style_bg_color(scroll_thumb_, lv_color_hex(0xC4B5FD), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(scroll_thumb_, lv_color_hex(0x818CF8), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(scroll_thumb_, LV_GRAD_DIR_VER, LV_PART_MAIN);
    update_scrollbar();
}

void ChatView::append_assistant_message(const std::string &text)
{
    if (!container_ || !fonts_)
        return;
    constexpr lv_coord_t bubble_width = 190;
    constexpr lv_coord_t text_width = 168;
    constexpr lv_coord_t pad_x = 10;
    constexpr lv_coord_t pad_y = 6;
    const lv_point_t text_size = measure_text(text, fonts_->font_12(), text_width);
    const lv_coord_t bubble_height = assistant_bubble_height(text_size.y);

    lv_obj_t *row = make_message_row(bubble_height, LV_FLEX_ALIGN_START);
    make_avatar(row);
    lv_obj_t *bubble = make_bubble(row, bubble_width, bubble_height, theme::kPanel, false);
    widgets::label(bubble, text, pad_x, pad_y, text_width,
                   bubble_height - pad_y * 2, fonts_->font_12(), theme::kWhite);
    scroll_to_bottom();
}

void ChatView::append_user_message(const std::string &text)
{
    if (!container_ || !fonts_)
        return;
    constexpr lv_coord_t text_max_width = 198 - kUserBubblePadX * 2;
    const lv_point_t text_size = measure_text(text, fonts_->font_12(), text_max_width);
    const ChatBubbleLayout layout = user_bubble_layout(text_size.x, text_size.y);
    const lv_coord_t bubble_width = layout.width;
    const lv_coord_t bubble_height = layout.height;

    lv_obj_t *row = make_message_row(bubble_height, LV_FLEX_ALIGN_END);
    lv_obj_t *bubble = make_bubble(row, bubble_width, bubble_height, theme::kIndigo, true);
    widgets::label(bubble, text, kUserBubblePadX, kUserBubblePadY,
                   bubble_width - kUserBubblePadX * 2,
                   bubble_height - kUserBubblePadY * 2,
                   fonts_->font_12(), theme::kWhite);
    scroll_to_bottom();
}

void ChatView::scroll(int delta)
{
    if (!container_)
        return;
    const int32_t top = lv_obj_get_scroll_top(container_);
    const int32_t bottom = lv_obj_get_scroll_bottom(container_);
    const int32_t applied = chat_scroll_delta(top, bottom, delta);
    if (applied == 0)
        return;
    lv_obj_scroll_by(container_, 0, applied, LV_ANIM_OFF);
    update_scrollbar();
}

void ChatView::scroll_page(int direction)
{
    if (!container_ || direction == 0)
        return;
    const int32_t page = lv_obj_get_height(container_);
    scroll(direction > 0 ? page : -page);
}

void ChatView::scroll_event(lv_event_t *event)
{
    ChatView *view = static_cast<ChatView *>(lv_event_get_user_data(event));
    if (view)
        view->update_scrollbar();
}

void ChatView::container_deleted(lv_event_t *event)
{
    ChatView *view = static_cast<ChatView *>(lv_event_get_user_data(event));
    if (view)
        view->clear_borrowed_objects();
}

lv_obj_t *ChatView::make_message_row(lv_coord_t height, lv_flex_align_t alignment) const
{
    lv_obj_t *row = lv_obj_create(container_);
    lv_obj_set_size(row, kChatRowWidth, height);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, alignment, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                      LV_OBJ_FLAG_SCROLLABLE));
    return row;
}

lv_obj_t *ChatView::make_bubble(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                                uint32_t color, bool user) const
{
    lv_obj_t *bubble = widgets::box(parent, 0, 0, width, height, color, 10);
    if (user) {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(theme::kPurple), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(bubble, lv_color_hex(theme::kIndigo), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(bubble, LV_GRAD_DIR_VER, LV_PART_MAIN);
    }
    return bubble;
}

void ChatView::make_avatar(lv_obj_t *parent) const
{
    lv_obj_t *avatar = lv_img_create(parent);
    lv_img_set_src(avatar, avatar_path_.c_str());
    lv_obj_set_pos(avatar, 0, 0);
    lv_obj_clear_flag(avatar, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                         LV_OBJ_FLAG_SCROLLABLE));
}

void ChatView::scroll_to_bottom()
{
    lv_obj_scroll_to_y(container_, LV_COORD_MAX, LV_ANIM_ON);
    update_scrollbar();
}

void ChatView::update_scrollbar()
{
    if (!container_ || !scroll_track_ || !scroll_thumb_)
        return;
    const int32_t top = lv_obj_get_scroll_top(container_);
    const int32_t bottom = lv_obj_get_scroll_bottom(container_);
    const ChatScrollbarLayout layout = chat_scrollbar_layout(top, bottom);
    lv_obj_set_pos(scroll_thumb_, 312, layout.y);
    lv_obj_set_size(scroll_thumb_, 3, layout.height);
}

void ChatView::release()
{
    if (container_) {
        lv_obj_remove_event_cb_with_user_data(container_, scroll_event, this);
        lv_obj_remove_event_cb_with_user_data(container_, container_deleted, this);
    }
    clear_borrowed_objects();
}

void ChatView::clear_borrowed_objects()
{
    fonts_ = nullptr;
    container_ = nullptr;
    scroll_track_ = nullptr;
    scroll_thumb_ = nullptr;
}

}  // namespace zclaw
