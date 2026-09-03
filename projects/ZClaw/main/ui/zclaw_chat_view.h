#pragma once

#include "lvgl/lvgl.h"

#include <string>

namespace zclaw {

class FontManager;

class ChatView {
public:
    ChatView() = default;
    ~ChatView();
    ChatView(const ChatView &) = delete;
    ChatView &operator=(const ChatView &) = delete;

    void create(lv_obj_t *parent, const FontManager *fonts, std::string avatar_path);
    void append_assistant_message(const std::string &text);
    void append_user_message(const std::string &text);
    void scroll(int delta);
    void scroll_page(int direction);

private:
    static void scroll_event(lv_event_t *event);
    static void container_deleted(lv_event_t *event);

    lv_obj_t *make_message_row(lv_coord_t height, lv_flex_align_t alignment) const;
    lv_obj_t *make_bubble(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                          uint32_t color, bool user) const;
    void make_avatar(lv_obj_t *parent) const;
    void scroll_to_bottom();
    void update_scrollbar();
    void release();
    void clear_borrowed_objects();

    const FontManager *fonts_ = nullptr;
    std::string avatar_path_;
    lv_obj_t *container_ = nullptr;
    lv_obj_t *scroll_track_ = nullptr;
    lv_obj_t *scroll_thumb_ = nullptr;
};

}  // namespace zclaw
