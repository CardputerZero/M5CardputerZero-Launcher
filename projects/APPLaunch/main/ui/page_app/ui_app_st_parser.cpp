/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_st.hpp"

#include <cctype>
#include <cstring>

void UISTPage::reset_terminal()
{
    cursor_ = Cursor{};
    cursor_.attr.fg = DEFAULT_FG;
    cursor_.attr.bg = DEFAULT_BG;
    saved_cursor_ = cursor_;
    scroll_top_ = 0;
    scroll_bot_ = term_rows_ - 1;
    scrollback_.clear();
    scrollback_offset_ = 0;
    mode_ = MODE_WRAP;
    parse_state_ = ParseState::Normal;
    utf8_decoder_.reset();
    cursor_visible_mode_ = true;
    csi_reset();
    for (auto &row : screen_)
        for (auto &cell : row) cell = blank_glyph();
    dirty_all();
}

void UISTPage::clear_region(int x1, int y1, int x2, int y2)
{
    x1 = clamp(x1, 0, term_cols_ - 1);
    x2 = clamp(x2, 0, term_cols_ - 1);
    y1 = clamp(y1, 0, term_rows_ - 1);
    y2 = clamp(y2, 0, term_rows_ - 1);
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);

    Glyph blank = blank_glyph();
    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            if (screen_[y][x].continuation && x > 0) screen_[y][x - 1] = blank;
            if (screen_[y][x].columns == 2 && x + 1 < term_cols_) screen_[y][x + 1] = blank;
            screen_[y][x] = blank;
        }
        normalize_wide_row(y);
        dirty_row(y);
    }
}

void UISTPage::normalize_wide_row(int row)
{
    if (row < 0 || row >= term_rows_) return;
    Glyph blank = blank_glyph();
    auto &line = screen_[row];
    for (int x = 0; x < term_cols_; ++x) {
        if (line[x].continuation) {
            if (x == 0 || line[x - 1].columns != 2 || line[x - 1].continuation)
                line[x] = blank;
            continue;
        }
        if (line[x].columns == 2) {
            if (x + 1 >= term_cols_) {
                line[x] = blank;
                continue;
            }
            Glyph continuation = line[x];
            continuation.u = ' ';
            continuation.combining_count = 0;
            continuation.columns = 0;
            continuation.continuation = true;
            line[x + 1] = continuation;
            ++x;
        }
    }
}

void UISTPage::move_to(int x, int y)
{
    cursor_.x = clamp(x, 0, term_cols_ - 1);
    cursor_.y = clamp(y, 0, term_rows_ - 1);
}

void UISTPage::scroll_up(int top, int bottom, int count)
{
    top = clamp(top, 0, term_rows_ - 1);
    bottom = clamp(bottom, 0, term_rows_ - 1);
    if (top > bottom || count <= 0) return;
    count = std::min(count, bottom - top + 1);
    if (!big_mode_ && top == 0 && bottom == term_rows_ - 1)
        for (int y = 0; y < count; ++y) append_scrollback_row(screen_[y]);
    for (int y = top; y <= bottom - count; ++y) {
        screen_[y] = screen_[y + count];
        dirty_row(y);
    }
    Glyph blank = blank_glyph();
    for (int y = bottom - count + 1; y <= bottom; ++y) {
        for (auto &cell : screen_[y]) cell = blank;
        dirty_row(y);
    }
}

void UISTPage::scroll_down(int top, int bottom, int count)
{
    top = clamp(top, 0, term_rows_ - 1);
    bottom = clamp(bottom, 0, term_rows_ - 1);
    if (top > bottom || count <= 0) return;
    count = std::min(count, bottom - top + 1);
    for (int y = bottom; y >= top + count; --y) {
        screen_[y] = screen_[y - count];
        dirty_row(y);
    }
    Glyph blank = blank_glyph();
    for (int y = top; y < top + count; ++y) {
        for (auto &cell : screen_[y]) cell = blank;
        dirty_row(y);
    }
}

void UISTPage::newline(bool first_col)
{
    if (first_col) cursor_.x = 0;
    if (cursor_.y == scroll_bot_)
        scroll_up(scroll_top_, scroll_bot_, 1);
    else
        cursor_.y = clamp(cursor_.y + 1, 0, term_rows_ - 1);
}

void UISTPage::put_tab()
{
    int next_tab = ((cursor_.x / 8) + 1) * 8;
    cursor_.x = clamp(next_tab, 0, term_cols_ - 1);
}

void UISTPage::insert_blank(int count)
{
    count = std::min(std::max(count, 1), term_cols_ - cursor_.x);
    auto &line = screen_[cursor_.y];
    for (int x = term_cols_ - 1; x >= cursor_.x + count; --x)
        line[x] = line[x - count];
    Glyph blank = blank_glyph();
    for (int x = cursor_.x; x < cursor_.x + count; ++x) line[x] = blank;
    normalize_wide_row(cursor_.y);
    dirty_row(cursor_.y);
}

void UISTPage::delete_chars(int count)
{
    count = std::min(std::max(count, 1), term_cols_ - cursor_.x);
    auto &line = screen_[cursor_.y];
    for (int x = cursor_.x; x < term_cols_ - count; ++x) line[x] = line[x + count];
    Glyph blank = blank_glyph();
    for (int x = term_cols_ - count; x < term_cols_; ++x) line[x] = blank;
    normalize_wide_row(cursor_.y);
    dirty_row(cursor_.y);
}

void UISTPage::put_rune(uint32_t rune)
{
    int width = terminal_codepoint_width(rune);
    if (width == 0) {
        int base_x = cursor_.x - 1;
        if (base_x >= 0 && screen_[cursor_.y][base_x].continuation) --base_x;
        if (base_x >= 0) {
            Glyph &base = screen_[cursor_.y][base_x];
            if (base.combining_count < base.combining.size())
                base.combining[base.combining_count++] = rune;
            dirty_row(cursor_.y);
            return;
        }
        width = 1;
    }
    if (mode_ & MODE_INSERT) insert_blank(width);
    if (cursor_.x >= term_cols_) {
        if (mode_ & MODE_WRAP) {
            cursor_.x = 0;
            newline(false);
        } else {
            cursor_.x = term_cols_ - 1;
        }
    }
    if (width == 2 && cursor_.x == term_cols_ - 1) {
        if (mode_ & MODE_WRAP) {
            cursor_.x = 0;
            newline(false);
        } else {
            width = 1;
        }
    }

    auto &line = screen_[cursor_.y];
    if (line[cursor_.x].continuation && cursor_.x > 0)
        line[cursor_.x - 1] = blank_glyph();
    if (line[cursor_.x].columns == 2 && cursor_.x + 1 < term_cols_)
        line[cursor_.x + 1] = blank_glyph();
    Glyph glyph = cursor_.attr;
    glyph.u = rune;
    glyph.columns = static_cast<uint8_t>(width);
    glyph.continuation = false;
    line[cursor_.x] = glyph;
    if (width == 2) {
        Glyph continuation = glyph;
        continuation.u = ' ';
        continuation.combining_count = 0;
        continuation.columns = 0;
        continuation.continuation = true;
        line[cursor_.x + 1] = continuation;
    }
    dirty_row(cursor_.y);
    cursor_.x += width;
}

void UISTPage::control_code(uint8_t code)
{
    switch (code) {
    case '\t': put_tab(); break;
    case '\b':
        cursor_.x = std::max(0, cursor_.x - 1);
        if (cursor_.x > 0 && screen_[cursor_.y][cursor_.x].continuation)
            --cursor_.x;
        break;
    case '\r': cursor_.x = 0; break;
    case '\n':
    case '\v':
    case '\f': newline(true); break;
    case 0x0e:
    case 0x0f:
    default: break;
    }
}

int UISTPage::param(int index, int default_value) const
{
    if (index >= csi_param_count_) return default_value;
    return csi_params_[index] == 0 ? default_value : csi_params_[index];
}

void UISTPage::csi_reset()
{
    csi_private_ = false;
    csi_secondary_ = false;
    csi_param_count_ = 0;
    csi_param_value_ = 0;
    csi_have_value_ = false;
    std::fill(std::begin(csi_params_), std::end(csi_params_), 0);
}

void UISTPage::csi_push_param()
{
    if (csi_param_count_ >= static_cast<int>(std::size(csi_params_))) return;
    csi_params_[csi_param_count_++] = csi_have_value_ ? csi_param_value_ : 0;
    csi_param_value_ = 0;
    csi_have_value_ = false;
}

void UISTPage::set_sgr()
{
    if (csi_param_count_ == 0) {
        cursor_.attr.attr = ATTR_NULL;
        cursor_.attr.fg = DEFAULT_FG;
        cursor_.attr.bg = DEFAULT_BG;
        return;
    }

    for (int i = 0; i < csi_param_count_; ++i) {
        int value = csi_params_[i];
        if (value == 0) {
            cursor_.attr.attr = ATTR_NULL;
            cursor_.attr.fg = DEFAULT_FG;
            cursor_.attr.bg = DEFAULT_BG;
        } else if (value == 1) {
            cursor_.attr.attr |= ATTR_BOLD;
        } else if (value == 2) {
            cursor_.attr.attr |= ATTR_FAINT;
        } else if (value == 4) {
            cursor_.attr.attr |= ATTR_UNDERLINE;
        } else if (value == 5) {
            cursor_.attr.attr |= ATTR_BLINK;
        } else if (value == 7) {
            cursor_.attr.attr |= ATTR_REVERSE;
        } else if (value == 8) {
            cursor_.attr.attr |= ATTR_INVISIBLE;
        } else if (value == 22) {
            cursor_.attr.attr &= ~(ATTR_BOLD | ATTR_FAINT);
        } else if (value == 24) {
            cursor_.attr.attr &= ~ATTR_UNDERLINE;
        } else if (value == 25) {
            cursor_.attr.attr &= ~ATTR_BLINK;
        } else if (value == 27) {
            cursor_.attr.attr &= ~ATTR_REVERSE;
        } else if (value == 28) {
            cursor_.attr.attr &= ~ATTR_INVISIBLE;
        } else if (value >= 30 && value <= 37) {
            cursor_.attr.fg = static_cast<uint32_t>(value - 30);
        } else if (value >= 40 && value <= 47) {
            cursor_.attr.bg = static_cast<uint32_t>(value - 40);
        } else if (value >= 90 && value <= 97) {
            cursor_.attr.fg = static_cast<uint32_t>(value - 90 + 8);
        } else if (value >= 100 && value <= 107) {
            cursor_.attr.bg = static_cast<uint32_t>(value - 100 + 8);
        } else if ((value == 38 || value == 48) && i + 2 < csi_param_count_ &&
                   csi_params_[i + 1] == 5) {
            uint32_t mapped = xterm256_to_palette(csi_params_[i + 2]);
            (value == 38 ? cursor_.attr.fg : cursor_.attr.bg) = mapped;
            i += 2;
        } else if ((value == 38 || value == 48) && i + 4 < csi_param_count_ &&
                   csi_params_[i + 1] == 2) {
            uint32_t mapped = rgb_to_palette(csi_params_[i + 2], csi_params_[i + 3],
                                             csi_params_[i + 4]);
            (value == 38 ? cursor_.attr.fg : cursor_.attr.bg) = mapped;
            i += 4;
        } else if (value == 39) {
            cursor_.attr.fg = DEFAULT_FG;
        } else if (value == 49) {
            cursor_.attr.bg = DEFAULT_BG;
        }
    }
}

void UISTPage::handle_private_mode(char final)
{
    bool set = final == 'h';
    for (int i = 0; i < csi_param_count_; ++i) {
        switch (csi_params_[i]) {
        case 1:
            if (set) mode_ |= MODE_APPCURSOR;
            else mode_ &= ~MODE_APPCURSOR;
            break;
        case 7:
            if (set) mode_ |= MODE_WRAP;
            else mode_ &= ~MODE_WRAP;
            break;
        case 25:
            cursor_visible_mode_ = set;
            break;
        case 1049:
            if (set) clear_region(0, 0, term_cols_ - 1, term_rows_ - 1);
            break;
        default:
            break;
        }
    }
}

void UISTPage::handle_csi(char final)
{
    if (csi_secondary_) {
        if (final == 'c') pty_write("\033[>0;115;0c", 11);
        return;
    }
    if (csi_private_ && (final == 'h' || final == 'l')) {
        handle_private_mode(final);
        return;
    }

    switch (final) {
    case '@': insert_blank(param(0, 1)); break;
    case 'A': move_to(cursor_.x, cursor_.y - param(0, 1)); break;
    case 'B': move_to(cursor_.x, cursor_.y + param(0, 1)); break;
    case 'C': move_to(cursor_.x + param(0, 1), cursor_.y); break;
    case 'D': move_to(cursor_.x - param(0, 1), cursor_.y); break;
    case 'G': move_to(param(0, 1) - 1, cursor_.y); break;
    case 'H':
    case 'f': move_to(param(1, 1) - 1, param(0, 1) - 1); break;
    case 'J':
        if (param(0, 0) == 0)
            clear_region(cursor_.x, cursor_.y, term_cols_ - 1, term_rows_ - 1);
        else if (param(0, 0) == 1)
            clear_region(0, 0, cursor_.x, cursor_.y);
        else
            clear_region(0, 0, term_cols_ - 1, term_rows_ - 1);
        break;
    case 'K':
        if (param(0, 0) == 0)
            clear_region(cursor_.x, cursor_.y, term_cols_ - 1, cursor_.y);
        else if (param(0, 0) == 1)
            clear_region(0, cursor_.y, cursor_.x, cursor_.y);
        else
            clear_region(0, cursor_.y, term_cols_ - 1, cursor_.y);
        break;
    case 'L': scroll_down(cursor_.y, scroll_bot_, param(0, 1)); break;
    case 'M': scroll_up(cursor_.y, scroll_bot_, param(0, 1)); break;
    case 'P': delete_chars(param(0, 1)); break;
    case 'd': move_to(cursor_.x, param(0, 1) - 1); break;
    case 'h':
        if (param(0, 0) == 4) mode_ |= MODE_INSERT;
        break;
    case 'l':
        if (param(0, 0) == 4) mode_ &= ~MODE_INSERT;
        break;
    case 'm': set_sgr(); break;
    case 'n':
        if (param(0, 0) == 5) {
            pty_write("\033[0n", 4);
        } else if (param(0, 0) == 6) {
            char reply[32];
            int length = snprintf(reply, sizeof(reply), "\033[%d;%dR",
                                  cursor_.y + 1, cursor_.x + 1);
            pty_write(reply, static_cast<size_t>(length));
        }
        break;
    case 'r':
        scroll_top_ = clamp(param(0, 1) - 1, 0, term_rows_ - 1);
        scroll_bot_ = clamp(param(1, term_rows_) - 1, 0, term_rows_ - 1);
        if (scroll_top_ >= scroll_bot_) {
            scroll_top_ = 0;
            scroll_bot_ = term_rows_ - 1;
        }
        move_to(0, 0);
        break;
    case 's': saved_cursor_ = cursor_; break;
    case 'u':
        cursor_ = saved_cursor_;
        move_to(cursor_.x, cursor_.y);
        break;
    case 'c': pty_write("\033[?1;2c", 7); break;
    default: break;
    }
}

void UISTPage::handle_esc(uint8_t code)
{
    switch (code) {
    case '[':
        csi_reset();
        parse_state_ = ParseState::Csi;
        return;
    case ']':
        parse_state_ = ParseState::Osc;
        return;
    case '(':
    case ')':
    case '*':
    case '+':
        parse_state_ = ParseState::Charset;
        return;
    case '7':
        saved_cursor_ = cursor_;
        break;
    case '8':
        cursor_ = saved_cursor_;
        move_to(cursor_.x, cursor_.y);
        break;
    case 'D': newline(false); break;
    case 'E': newline(true); break;
    case 'M':
        if (cursor_.y == scroll_top_)
            scroll_down(scroll_top_, scroll_bot_, 1);
        else
            move_to(cursor_.x, cursor_.y - 1);
        break;
    case 'c': reset_terminal(); break;
    default: break;
    }
    parse_state_ = ParseState::Normal;
}

void UISTPage::process_bytes(const char *data, int length)
{
    for (int index = 0; index < length; ++index) {
        uint8_t code = static_cast<uint8_t>(data[index]);
        if (parse_state_ == ParseState::Normal && utf8_decoder_.pending()) {
            const TerminalUtf8Result decoded = utf8_decoder_.feed(code);
            if (decoded.emitted) put_rune(decoded.codepoint);
            if (decoded.retry_byte) --index;
            continue;
        }
        if (parse_state_ == ParseState::Osc) {
            if (code == 0x07) {
                parse_state_ = ParseState::Normal;
            } else if (code == 0x1b && index + 1 < length && data[index + 1] == '\\') {
                ++index;
                parse_state_ = ParseState::Normal;
            }
            continue;
        }
        if (parse_state_ == ParseState::Charset) {
            parse_state_ = ParseState::Normal;
            continue;
        }
        if (parse_state_ == ParseState::Esc) {
            handle_esc(code);
            continue;
        }
        if (parse_state_ == ParseState::Csi) {
            if (code == '?') {
                csi_private_ = true;
                continue;
            }
            if (code == '>') {
                csi_secondary_ = true;
                continue;
            }
            if (std::isdigit(code)) {
                csi_param_value_ = csi_param_value_ * 10 + (code - '0');
                csi_have_value_ = true;
                continue;
            }
            if (code == ';') {
                csi_push_param();
                continue;
            }
            if (code >= 0x20 && code <= 0x2f) continue;
            csi_push_param();
            handle_csi(static_cast<char>(code));
            parse_state_ = ParseState::Normal;
            continue;
        }

        if (code == 0x1b)
            parse_state_ = ParseState::Esc;
        else if (code < 0x20 || code == 0x7f)
            control_code(code);
        else if (code < 0x80)
            put_rune(code);
        else {
            const TerminalUtf8Result decoded = utf8_decoder_.feed(code);
            if (decoded.emitted) put_rune(decoded.codepoint);
        }
    }
}
