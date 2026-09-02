/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "sample_log.h"
#include "../launcher_ui_app_page.hpp"
#include "../model/page_timer_lifecycle.hpp"
#include "../model/st_page_contract.hpp"
#include "../model/terminal_unicode.hpp"
#include "cp0_lvgl_app.h"
#include "input_keys.h"
#include <algorithm>
#include <array>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list>
#include <string>
#include <vector>
#include <chrono>

// ============================================================
//  ST terminal  UISTPage
//  Screen resolution: 320 x 170  (top bar 20px, ui_APP_Container 320x150)
//
//  This is a standalone terminal page. It does not inherit from or include
//  the existing console page. The structure follows suckless st's split:
//    - terminal state: glyph grid, cursor, modes, scroll region
//    - byte stream parser: ESC/CSI/OSC handling
//    - frontend: LVGL row renderer and keyboard -> PTY writer
// ============================================================
class UISTPage : public AppPage
{
    enum class FontSize : uint8_t {
        Terminal = 12,
    };

    static constexpr int font_size(FontSize size)
    {
        return static_cast<int>(size);
    }

    static constexpr int TERM_W = 320;
    static constexpr int TERM_H = 150;
    // JetBrains Mono Bold at 12px resolves to a 7px advance and 15px line height in LVGL.
    static constexpr int CHAR_W = 7;
    static constexpr int CHAR_H = 15;
    // Descenders in the 12px Bold face need the full 15px cell height.
    static constexpr int TEXT_Y_PAD = 0;
    static constexpr int NORMAL_COLS = TERM_W / CHAR_W;
    static constexpr int NORMAL_ROWS = TERM_H / CHAR_H;
    static constexpr int BIG_COLS = 80;
    static constexpr int BIG_ROWS = 24;
    static constexpr int BIG_BOTTOM_H = 15;
    static constexpr int BIG_VIEW_ROWS = (TERM_H - BIG_BOTTOM_H) / CHAR_H;
    static constexpr int MAX_COLS = BIG_COLS;
    static constexpr int MAX_ROWS = BIG_ROWS;
    static constexpr int COLS = NORMAL_COLS;
    static constexpr int ROWS = NORMAL_ROWS;
    static constexpr int SCROLLBACK_MAX_ROWS = 200;
    static constexpr int SCROLLBAR_W = 3;
    static constexpr int BOTTOM_BAR_SLOTS = 5;

    static constexpr uint32_t DEFAULT_FG = 7;
    static constexpr uint32_t DEFAULT_BG = 0;

    enum GlyphAttr : uint16_t {
        ATTR_NULL      = 0,
        ATTR_BOLD      = 1 << 0,
        ATTR_FAINT     = 1 << 1,
        ATTR_UNDERLINE = 1 << 2,
        ATTR_BLINK     = 1 << 3,
        ATTR_REVERSE   = 1 << 4,
        ATTR_INVISIBLE = 1 << 5,
    };

    enum TermMode : uint16_t {
        MODE_WRAP      = 1 << 0,
        MODE_INSERT    = 1 << 1,
        MODE_APPCURSOR = 1 << 2,
    };

    enum class ParseState {
        Normal,
        Esc,
        Csi,
        Osc,
        Charset,
    };

    struct Glyph {
        uint32_t u = ' ';
        std::array<uint32_t, 2> combining{};
        uint8_t combining_count = 0;
        uint8_t columns = 1;
        bool continuation = false;
        uint16_t attr = ATTR_NULL;
        uint32_t fg = DEFAULT_FG;
        uint32_t bg = DEFAULT_BG;
    };

    struct Cursor {
        int x = 0;
        int y = 0;
        Glyph attr;
    };

    struct RenderSegment {
        lv_obj_t *label = nullptr;
        std::string text;
        int x = 0;
        int width = 0;
        uint32_t fg = UINT32_MAX;
        uint32_t bg = UINT32_MAX;
        bool hidden = true;
    };

    struct SegmentData {
        std::string text;
        int x = 0;
        int columns = 0;
        uint32_t fg = DEFAULT_FG;
        uint32_t bg = DEFAULT_BG;
    };

public:
    bool terminal_sysplause = true;

    UISTPage();
    ~UISTPage();

    void exec(std::string cmd);
    void exec(const std::string &command, const std::list<std::string> &arguments);

private:
    std::array<std::array<Glyph, MAX_COLS>, MAX_ROWS> screen_{};
    std::array<std::vector<RenderSegment>, ROWS> row_segments_{};
    std::array<bool, ROWS> dirty_{};
    std::vector<std::array<Glyph, MAX_COLS>> scrollback_;
    int scrollback_offset_ = 0;
    int term_cols_ = NORMAL_COLS;
    int term_rows_ = NORMAL_ROWS;
    int viewport_x_ = 0;
    int viewport_y_ = 0;
    bool big_view_locked_ = false;
    Cursor cursor_{};
    Cursor saved_cursor_{};
    int scroll_top_ = 0;
    int scroll_bot_ = NORMAL_ROWS - 1;
    uint16_t mode_ = MODE_WRAP;

    ParseState parse_state_ = ParseState::Normal;
    TerminalUtf8Decoder utf8_decoder_;
    bool csi_private_ = false;
    bool csi_secondary_ = false;
    int csi_params_[16] = {};
    int csi_param_count_ = 0;
    int csi_param_value_ = 0;
    bool csi_have_value_ = false;

    lv_obj_t *terminal_container_ = nullptr;
    lv_obj_t *term_canvas_ = nullptr;
    lv_obj_t *scrollbar_track_ = nullptr;
    lv_obj_t *scrollbar_thumb_ = nullptr;
    lv_obj_t *hscrollbar_track_ = nullptr;
    lv_obj_t *hscrollbar_thumb_ = nullptr;
    std::array<lv_obj_t *, BOTTOM_BAR_SLOTS> bottom_labels_{};
    std::array<lv_obj_t *, BOTTOM_BAR_SLOTS> bottom_indicators_{};
    const lv_font_t *mono_font_ = nullptr;
    lv_obj_t *cursor_label_ = nullptr;
    PageTimerLifecycle<lv_timer_t *> poll_timer_;
    PageTimerLifecycle<lv_timer_t *> cursor_timer_;

    std::string pty_handle_;
    bool terminal_active_ = false;
    bool waiting_key_to_exit_ = false;
    bool cursor_blink_visible_ = false;
    bool cursor_visible_mode_ = true;
    bool shift_down_ = false;
    bool big_mode_ = false;
    int home_hold_status_ = 0;
    std::chrono::time_point<std::chrono::steady_clock> home_hold_start_{};

    static int clamp(int v, int lo, int hi);
    static std::string printable(uint32_t u);
    static lv_color_t palette(uint32_t color);
    static const lv_font_t *terminal_font();
    static uint32_t xterm256_to_palette(int color);
    static uint32_t rgb_to_palette(int r, int g, int b);

    Glyph blank_glyph() const;
    void dirty_row(int row);
    void dirty_all();
    int visible_cols() const;
    int visible_rows() const;
    int visible_h() const;
    int max_viewport_x() const;
    int max_viewport_y() const;
    void append_scrollback_row(const std::array<Glyph, MAX_COLS> &row);
    const std::array<Glyph, MAX_COLS> &display_row(int row) const;
    void scrollback_page(int direction);

    void update_scrollbar();
    void update_hscrollbar();
    void set_bottom_label(int index, const char *text);
    void update_big_mode_ui();
    void switch_big_mode(bool enable);
    void leave_scrollback();
    void pan_big_view(int dx, int dy);
    void follow_cursor_in_big_mode();

    void reset_terminal();
    void clear_region(int x1, int y1, int x2, int y2);
    void normalize_wide_row(int row);
    void move_to(int x, int y);
    void scroll_up(int top, int bottom, int count);
    void scroll_down(int top, int bottom, int count);
    void newline(bool first_col);
    void put_tab();
    void insert_blank(int count);
    void delete_chars(int count);
    void put_rune(uint32_t rune);
    void control_code(uint8_t code);

    int param(int index, int default_value) const;
    void csi_reset();
    void csi_push_param();
    void set_sgr();
    void handle_private_mode(char final);

    void handle_csi(char final);

    void handle_esc(uint8_t code);
    void process_bytes(const char *data, int length);

    void create_ui();
    bool renderer_ready() const;
    void bind_events();
    void detach_renderer_callbacks();
    static void static_renderer_delete_cb(lv_event_t *event) noexcept;
    static void static_event_cb(lv_event_t *event) noexcept;
    void event_cb(lv_event_t *event);
    void recover_callback_failure() noexcept;

    static void effective_colors(const Glyph &glyph, uint32_t *foreground, uint32_t *background);
    bool meaningful_cell(const Glyph &glyph) const;
    lv_obj_t *create_segment_label();
    std::vector<SegmentData> build_row_segments(int row);
    void render_row(int row);
    void render_all();
    void update_cursor();
    void show_cursor(bool show);

    std::string pty_open(const std::string &command, const std::list<std::string> &args);
    void stop_timers();
    bool start_timers();
    void start_command(const std::string &command, const std::list<std::string> &args,
                       const char *title, const char *error_message);
    void start_shell();
    void stop_pty();
    int pty_read(char *buffer, size_t buffer_size);
    int pty_write(const char *buffer, size_t length);
    int pty_check_child(int *status);
    static void static_poll_cb(lv_timer_t *timer) noexcept;
    void poll_cb();
    static void static_cursor_cb(lv_timer_t *timer) noexcept;
    void cursor_cb();
    void handle_home_hold_exit();
    void write_key(uint32_t evdev_key, const char *utf8);
};
