/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_st.hpp"
#include "../model/st_key_encoder.hpp"

void UISTPage::create_ui()
{
    if (!ui_APP_Container) return;
    terminal_container_ = lv_obj_create(ui_APP_Container);
    if (!terminal_container_) return;
    lv_obj_add_event_cb(terminal_container_, static_renderer_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(terminal_container_);
    lv_obj_set_size(terminal_container_, TERM_W, TERM_H);
    lv_obj_set_pos(terminal_container_, 0, 0);
    lv_obj_set_style_bg_color(terminal_container_, palette(DEFAULT_BG), 0);
    lv_obj_set_style_bg_opa(terminal_container_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(terminal_container_,
                      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    term_canvas_ = lv_obj_create(terminal_container_);
    if (!term_canvas_) return;
    lv_obj_add_event_cb(term_canvas_, static_renderer_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(term_canvas_, TERM_W, TERM_H);
    lv_obj_set_pos(term_canvas_, 0, 0);
    lv_obj_set_style_bg_color(term_canvas_, palette(DEFAULT_BG), 0);
    lv_obj_set_style_bg_opa(term_canvas_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(term_canvas_, 0, 0);
    lv_obj_set_style_pad_all(term_canvas_, 0, 0);
    lv_obj_set_style_radius(term_canvas_, 0, 0);
    lv_obj_clear_flag(term_canvas_,
                      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    auto create_scrollbar = [&](lv_obj_t **object, int width, int height, int x, int y,
                                uint32_t color) {
        *object = lv_obj_create(terminal_container_);
        if (!*object) return;
        lv_obj_add_event_cb(*object, static_renderer_delete_cb, LV_EVENT_DELETE, this);
        lv_obj_remove_style_all(*object);
        lv_obj_set_size(*object, width, height);
        lv_obj_set_pos(*object, x, y);
        lv_obj_set_style_bg_color(*object, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(*object, LV_OPA_COVER, 0);
        lv_obj_clear_flag(*object,
                          static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                     LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_add_flag(*object, LV_OBJ_FLAG_HIDDEN);
    };
    create_scrollbar(&scrollbar_track_, SCROLLBAR_W, TERM_H,
                     TERM_W - SCROLLBAR_W - 1, 0, 0x30363D);
    create_scrollbar(&scrollbar_thumb_, SCROLLBAR_W, 8,
                     TERM_W - SCROLLBAR_W - 1, TERM_H - 8, 0x8B949E);
    create_scrollbar(&hscrollbar_track_, TERM_W - SCROLLBAR_W - 2, 3,
                     0, BIG_VIEW_ROWS * CHAR_H, 0x30363D);
    create_scrollbar(&hscrollbar_thumb_, 18, 3,
                     0, BIG_VIEW_ROWS * CHAR_H, 0x8B949E);

    static constexpr const char *BOTTOM_TEXT[BOTTOM_BAR_SLOTS] = {
        "F4 <", "F5 up", "F6 normal", "F7 down", "F8 >",
    };
    constexpr int SLOT_WIDTH = TERM_W / BOTTOM_BAR_SLOTS;
    for (int index = 0; index < BOTTOM_BAR_SLOTS; ++index) {
        size_t slot = static_cast<size_t>(index);
        bottom_labels_[slot] = lv_label_create(terminal_container_);
        if (bottom_labels_[slot]) {
            lv_obj_add_event_cb(bottom_labels_[slot], static_renderer_delete_cb, LV_EVENT_DELETE, this);
            lv_obj_set_pos(bottom_labels_[slot], index * SLOT_WIDTH, TERM_H - BIG_BOTTOM_H);
            lv_obj_set_size(bottom_labels_[slot], SLOT_WIDTH, BIG_BOTTOM_H);
            lv_obj_set_style_text_font(bottom_labels_[slot], &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(bottom_labels_[slot], lv_color_hex(0xF0F6FC), 0);
            lv_obj_set_style_text_align(bottom_labels_[slot], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_pad_top(bottom_labels_[slot], 1, 0);
            lv_label_set_text(bottom_labels_[slot], BOTTOM_TEXT[index]);
            lv_obj_add_flag(bottom_labels_[slot], LV_OBJ_FLAG_HIDDEN);
        }

        bottom_indicators_[slot] = lv_label_create(terminal_container_);
        if (bottom_indicators_[slot]) {
            lv_obj_add_event_cb(bottom_indicators_[slot], static_renderer_delete_cb, LV_EVENT_DELETE, this);
            lv_obj_set_pos(bottom_indicators_[slot], index * SLOT_WIDTH, TERM_H - 4);
            lv_obj_set_size(bottom_indicators_[slot], SLOT_WIDTH, 4);
            lv_obj_set_style_text_font(bottom_indicators_[slot], &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(bottom_indicators_[slot], lv_color_hex(0x8B949E), 0);
            lv_obj_set_style_text_align(bottom_indicators_[slot], LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(bottom_indicators_[slot], "|");
            lv_obj_add_flag(bottom_indicators_[slot], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_font_t *primary = launcher_fonts().get_mono(
        "JetBrainsMono-Regular.ttf", 11, LV_FREETYPE_FONT_STYLE_NORMAL);
    lv_font_t *cjk = launcher_fonts().get(
        "AlibabaPuHuiTi-3-55-Regular.ttf", 11, LV_FREETYPE_FONT_STYLE_NORMAL);
    lv_font_t *symbols = launcher_fonts().get(
        "DejaVuSans.ttf", 11, LV_FREETYPE_FONT_STYLE_NORMAL);
    if (cjk && symbols && cjk != symbols) cjk->fallback = symbols;
    if (primary && cjk && primary != cjk) primary->fallback = cjk;
    mono_font_ = primary;
    cursor_label_ = lv_label_create(term_canvas_);
    if (!cursor_label_) return;
    lv_obj_add_event_cb(cursor_label_, static_renderer_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_style_text_font(cursor_label_, mono_font_, 0);
    lv_obj_set_style_text_color(cursor_label_, palette(DEFAULT_BG), 0);
    lv_obj_set_style_bg_color(cursor_label_, palette(DEFAULT_FG), 0);
    lv_obj_set_style_bg_opa(cursor_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(cursor_label_, 0, 0);
    lv_obj_set_style_pad_top(cursor_label_, TEXT_Y_PAD, 0);
    lv_obj_set_style_text_letter_space(cursor_label_, 0, 0);
    lv_label_set_long_mode(cursor_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(cursor_label_, CHAR_W, CHAR_H);
    lv_label_set_text(cursor_label_, " ");
    lv_obj_add_flag(cursor_label_, LV_OBJ_FLAG_HIDDEN);
}

void UISTPage::bind_events()
{
    if (!root_screen_) return;
    lv_obj_add_event_cb(root_screen_, UISTPage::static_event_cb, LV_EVENT_ALL, this);
}

bool UISTPage::renderer_ready() const
{
    if (!st_page_renderer_ready(terminal_container_, term_canvas_, scrollbar_track_,
                                scrollbar_thumb_, hscrollbar_track_, hscrollbar_thumb_,
                                cursor_label_))
        return false;
    for (lv_obj_t *label : bottom_labels_)
        if (!label) return false;
    for (lv_obj_t *indicator : bottom_indicators_)
        if (!indicator) return false;
    return true;
}

void UISTPage::detach_renderer_callbacks()
{
    std::vector<lv_obj_t *> objects = {
        terminal_container_, term_canvas_, scrollbar_track_, scrollbar_thumb_,
        hscrollbar_track_, hscrollbar_thumb_, cursor_label_,
    };
    objects.insert(objects.end(), bottom_labels_.begin(), bottom_labels_.end());
    objects.insert(objects.end(), bottom_indicators_.begin(), bottom_indicators_.end());
    for (const auto &row : row_segments_)
        for (const auto &segment : row)
            objects.push_back(segment.label);
    for (lv_obj_t *object : objects)
        if (object)
            lv_obj_remove_event_cb_with_user_data(object, static_renderer_delete_cb, this);
}

void UISTPage::static_renderer_delete_cb(lv_event_t *event) noexcept
{
    auto *self = static_cast<UISTPage *>(lv_event_get_user_data(event));
    if (!self) return;
    try {
        auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (!deleted || deleted != lv_event_get_current_target(event)) return;

        if (self->cursor_label_ == deleted) self->cursor_label_ = nullptr;
        if (self->scrollbar_track_ == deleted) self->scrollbar_track_ = nullptr;
        if (self->scrollbar_thumb_ == deleted) self->scrollbar_thumb_ = nullptr;
        if (self->hscrollbar_track_ == deleted) self->hscrollbar_track_ = nullptr;
        if (self->hscrollbar_thumb_ == deleted) self->hscrollbar_thumb_ = nullptr;
        for (lv_obj_t *&label : self->bottom_labels_)
            if (label == deleted) label = nullptr;
        for (lv_obj_t *&indicator : self->bottom_indicators_)
            if (indicator == deleted) indicator = nullptr;
        for (auto &row : self->row_segments_)
            for (auto &segment : row)
                if (segment.label == deleted) {
                    segment.label = nullptr;
                    segment.hidden = true;
                    segment.text.clear();
                }
        if (self->term_canvas_ == deleted) {
            self->terminal_active_ = false;
            self->stop_timers();
            self->stop_pty();
            self->term_canvas_ = nullptr;
            self->cursor_label_ = nullptr;
            for (auto &row : self->row_segments_)
                for (auto &segment : row)
                    segment.label = nullptr;
        }
        if (self->terminal_container_ == deleted) {
            self->terminal_active_ = false;
            self->stop_timers();
            self->stop_pty();
            self->terminal_container_ = nullptr;
            self->term_canvas_ = nullptr;
            self->scrollbar_track_ = nullptr;
            self->scrollbar_thumb_ = nullptr;
            self->hscrollbar_track_ = nullptr;
            self->hscrollbar_thumb_ = nullptr;
            self->cursor_label_ = nullptr;
            self->bottom_labels_.fill(nullptr);
            self->bottom_indicators_.fill(nullptr);
            for (auto &row : self->row_segments_)
                for (auto &segment : row)
                    segment.label = nullptr;
        }
        if (!self->renderer_ready()) {
            self->terminal_active_ = false;
            self->stop_timers();
            self->stop_pty();
        }
    } catch (...) {
        self->recover_callback_failure();
    }
}

void UISTPage::static_event_cb(lv_event_t *event) noexcept
{
    auto *self = static_cast<UISTPage *>(lv_event_get_user_data(event));
    if (!self) return;
    try {
        self->event_cb(event);
    } catch (...) {
        self->recover_callback_failure();
    }
}

void UISTPage::recover_callback_failure() noexcept
{
    const auto state = st_callback_failure_state();
    terminal_active_ = state.terminal_active;
    waiting_key_to_exit_ = state.waiting_key_to_exit;
    home_hold_status_ = state.home_hold_status;
}

void UISTPage::event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_DELETE &&
        lv_event_get_target(event) == lv_event_get_current_target(event)) {
        terminal_active_ = false;
        stop_timers();
        stop_pty();
        terminal_container_ = nullptr;
        term_canvas_ = nullptr;
        scrollbar_track_ = nullptr;
        scrollbar_thumb_ = nullptr;
        hscrollbar_track_ = nullptr;
        hscrollbar_thumb_ = nullptr;
        cursor_label_ = nullptr;
        bottom_labels_.fill(nullptr);
        bottom_indicators_.fill(nullptr);
        for (auto &row : row_segments_)
            for (auto &segment : row)
                segment.label = nullptr;
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_KEYBOARD) return;
    auto *key = static_cast<key_item *>(lv_event_get_param(event));
    if (!key) return;

    if (waiting_key_to_exit_) {
        if (key->key_state == 0) {
            if (terminal_sysplause)
                terminal_sysplause = false;
            else {
                waiting_key_to_exit_ = false;
                if (navigate_home) navigate_home();
            }
        }
        return;
    }
    if (key->key_code == KEY_LEFTSHIFT || key->key_code == KEY_RIGHTSHIFT) {
        shift_down_ = key->key_state != KBD_KEY_RELEASED;
        return;
    }
    if (key->key_state && key->key_code == KEY_F6) {
        switch_big_mode(!big_mode_);
        return;
    }
    if (key->key_state && big_mode_) {
        switch (key->key_code) {
        case KEY_F4: pan_big_view(-8, 0); render_all(); return;
        case KEY_F8: pan_big_view(8, 0); render_all(); return;
        case KEY_F5: pan_big_view(0, -4); render_all(); return;
        case KEY_F7: pan_big_view(0, 4); render_all(); return;
        default: break;
        }
    }

    const int scrollback_direction = STKeyEncoder::scrollback_direction(
        key->key_code, key->mods, shift_down_);
    if (key->key_state && scrollback_direction != 0) {
        scrollback_page(scrollback_direction);
        render_all();
        return;
    }
    if (terminal_active_ && !pty_handle_.empty() &&
        STKeyEncoder::should_forward_event(key->key_code, key->key_state)) {
        leave_scrollback();
        follow_cursor_in_big_mode();
        write_key(key->key_code, key->utf8);
    }
}

void UISTPage::effective_colors(const Glyph &glyph, uint32_t *foreground,
                                uint32_t *background)
{
    uint32_t output_foreground = glyph.fg;
    uint32_t output_background = glyph.bg;
    if (glyph.attr & ATTR_REVERSE) std::swap(output_foreground, output_background);
    if (glyph.attr & ATTR_BOLD)
        output_foreground = output_foreground < 8 ? output_foreground + 8 : output_foreground;
    if (foreground) *foreground = output_foreground;
    if (background) *background = output_background;
}

bool UISTPage::meaningful_cell(const Glyph &glyph) const
{
    uint32_t foreground = DEFAULT_FG;
    uint32_t background = DEFAULT_BG;
    effective_colors(glyph, &foreground, &background);
    const std::string character = glyph.attr & ATTR_INVISIBLE ? " " : printable(glyph.u);
    return (!glyph.continuation && character != " ") ||
           foreground != DEFAULT_FG || background != DEFAULT_BG;
}

lv_obj_t *UISTPage::create_segment_label()
{
    if (!term_canvas_) return nullptr;
    lv_obj_t *label = lv_label_create(term_canvas_);
    if (!label) return nullptr;
    lv_obj_add_event_cb(label, static_renderer_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_style_text_font(label, mono_font_, 0);
    lv_obj_set_style_text_color(label, palette(DEFAULT_FG), 0);
    lv_obj_set_style_bg_color(label, palette(DEFAULT_BG), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_pad_top(label, TEXT_Y_PAD, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_set_style_text_line_space(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(label, CHAR_W, CHAR_H);
    lv_label_set_text(label, " ");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}

std::vector<UISTPage::SegmentData> UISTPage::build_row_segments(int row)
{
    std::vector<SegmentData> result;
    const auto &line = display_row(row);
    int first_column = big_mode_ ? viewport_x_ : 0;
    int last_column = -1;
    for (int column = visible_cols() - 1; column >= 0; --column) {
        if (meaningful_cell(line[static_cast<size_t>(first_column + column)])) {
            last_column = column;
            break;
        }
    }
    if (last_column < 0) return result;

    SegmentData current;
    bool has_current = false;
    auto flush = [&]() {
        if (has_current) result.push_back(current);
        current = SegmentData{};
        has_current = false;
    };
    for (int column = 0; column <= last_column; ++column) {
        const Glyph &glyph = line[static_cast<size_t>(first_column + column)];
        uint32_t foreground = DEFAULT_FG;
        uint32_t background = DEFAULT_BG;
        effective_colors(glyph, &foreground, &background);
        if (glyph.continuation && column > 0 &&
            !line[static_cast<size_t>(first_column + column - 1)].continuation)
            continue;
        std::string character = glyph.attr & ATTR_INVISIBLE ? " " : printable(glyph.u);
        if (!(glyph.attr & ATTR_INVISIBLE))
            for (uint8_t index = 0; index < glyph.combining_count; ++index)
                terminal_utf8_append(character, glyph.combining[index]);
        const int columns = glyph.continuation ? 1 : std::max<int>(1, glyph.columns);
        const bool isolated = glyph.u > 0x7f || glyph.combining_count != 0 || columns != 1;
        if (isolated) {
            flush();
            current = SegmentData{};
            current.x = column;
            current.columns = columns;
            current.fg = foreground;
            current.bg = background;
            current.text = std::move(character);
            result.push_back(current);
            continue;
        }
        if (!has_current || current.fg != foreground || current.bg != background) {
            flush();
            current.x = column;
            current.fg = foreground;
            current.bg = background;
            has_current = true;
        }
        current.text += character;
        ++current.columns;
    }
    flush();
    return result;
}

void UISTPage::render_row(int row)
{
    std::vector<SegmentData> desired = build_row_segments(row);
    std::vector<RenderSegment> &rendered = row_segments_[row];
    if (rendered.size() < desired.size()) rendered.resize(desired.size());

    for (size_t index = 0; index < desired.size(); ++index) {
        SegmentData &wanted = desired[index];
        RenderSegment &current = rendered[index];
        if (!current.label) current.label = create_segment_label();
        if (!current.label) continue;
        int width = wanted.columns * CHAR_W;
        bool changed = current.hidden || current.x != wanted.x || current.width != width ||
                       current.fg != wanted.fg || current.bg != wanted.bg ||
                       current.text != wanted.text;
        if (!changed) continue;

        current.hidden = false;
        current.x = wanted.x;
        current.width = width;
        current.fg = wanted.fg;
        current.bg = wanted.bg;
        current.text = wanted.text;
        lv_obj_clear_flag(current.label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(current.label, wanted.x * CHAR_W, row * CHAR_H);
        lv_obj_set_size(current.label, width, CHAR_H);
        lv_obj_set_style_text_color(current.label, palette(wanted.fg), 0);
        lv_obj_set_style_bg_color(current.label, palette(wanted.bg), 0);
        lv_label_set_text(current.label, wanted.text.c_str());
    }
    for (size_t index = desired.size(); index < rendered.size(); ++index) {
        RenderSegment &segment = rendered[index];
        if (!segment.hidden && segment.label) {
            lv_obj_add_flag(segment.label, LV_OBJ_FLAG_HIDDEN);
            segment.hidden = true;
            segment.text.clear();
        }
    }
}

void UISTPage::render_all()
{
    int visible_row_count = visible_rows();
    for (int row = 0; row < ROWS; ++row) {
        if (row >= visible_row_count) {
            if (dirty_[row]) {
                for (auto &segment : row_segments_[row]) {
                    if (segment.label) lv_obj_add_flag(segment.label, LV_OBJ_FLAG_HIDDEN);
                    segment.hidden = true;
                }
                dirty_[row] = false;
            }
        } else if (dirty_[row]) {
            render_row(row);
            dirty_[row] = false;
        }
    }
    update_cursor();
    update_hscrollbar();
    update_scrollbar();
}

void UISTPage::update_cursor()
{
    if (!cursor_label_) return;
    if (scrollback_offset_ > 0) {
        lv_obj_add_flag(cursor_label_, LV_OBJ_FLAG_HIDDEN);
        cursor_blink_visible_ = false;
        return;
    }
    int x = clamp(cursor_.x - (big_mode_ ? viewport_x_ : 0), 0, visible_cols() - 1);
    int y = clamp(cursor_.y - (big_mode_ ? viewport_y_ : 0), 0, visible_rows() - 1);
    bool outside_view = big_mode_ &&
        (cursor_.x < viewport_x_ || cursor_.x >= viewport_x_ + visible_cols() ||
         cursor_.y < viewport_y_ || cursor_.y >= viewport_y_ + visible_rows());
    if (outside_view) {
        lv_obj_add_flag(cursor_label_, LV_OBJ_FLAG_HIDDEN);
        cursor_blink_visible_ = false;
        return;
    }

    const Glyph &glyph = screen_[cursor_.y][cursor_.x];
    uint32_t foreground = DEFAULT_FG;
    uint32_t background = DEFAULT_BG;
    effective_colors(glyph, &foreground, &background);
    std::string text = glyph.attr & ATTR_INVISIBLE || glyph.continuation
        ? " " : printable(glyph.u);
    if (!(glyph.attr & ATTR_INVISIBLE) && !glyph.continuation)
        for (uint8_t index = 0; index < glyph.combining_count; ++index)
            terminal_utf8_append(text, glyph.combining[index]);
    lv_label_set_text(cursor_label_, text.c_str());
    lv_obj_set_style_text_color(cursor_label_, palette(background), 0);
    lv_obj_set_style_bg_color(cursor_label_, palette(foreground), 0);
    lv_obj_set_pos(cursor_label_, x * CHAR_W, y * CHAR_H);
    lv_obj_set_size(cursor_label_, std::max<int>(1, glyph.columns) * CHAR_W, CHAR_H);
    lv_obj_move_foreground(cursor_label_);
}

void UISTPage::show_cursor(bool show)
{
    if (!cursor_label_) return;
    if (scrollback_offset_ > 0) show = false;
    if (big_mode_ &&
        (cursor_.x < viewport_x_ || cursor_.x >= viewport_x_ + visible_cols() ||
         cursor_.y < viewport_y_ || cursor_.y >= viewport_y_ + visible_rows()))
        show = false;
    if (show) lv_obj_clear_flag(cursor_label_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(cursor_label_, LV_OBJ_FLAG_HIDDEN);
    cursor_blink_visible_ = show;
}
