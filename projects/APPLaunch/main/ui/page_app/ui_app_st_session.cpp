/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_st.hpp"
#include "cp0_esc_state.h"
#include "../model/st_key_encoder.hpp"

#include <cstdlib>
#include <cstring>

std::string UISTPage::pty_open(const std::string &command,
                               const std::list<std::string> &args)
{
    int result = -1;
    std::string handle;
    std::list<std::string> api_args = {
        "Open", command, std::to_string(term_cols_), std::to_string(term_rows_), command,
    };
    for (const auto &arg : args) api_args.push_back(arg);
    cp0_signal_pty_api(std::move(api_args), [&](int code, std::string data) {
        result = code;
        if (result == 0) handle = std::move(data);
    });
    return handle;
}

void UISTPage::stop_timers()
{
    poll_timer_.stop();
    cursor_timer_.stop();
}

bool UISTPage::start_timers()
{
    const bool poll_started = poll_timer_.start(
        [this] { return lv_timer_create(UISTPage::static_poll_cb, 30, this); },
        [](lv_timer_t *timer) { lv_timer_delete(timer); });
    const bool cursor_started = cursor_timer_.start(
        [this] { return lv_timer_create(UISTPage::static_cursor_cb, 500, this); },
        [](lv_timer_t *timer) { lv_timer_delete(timer); });
    if (poll_started && cursor_started) return true;
    stop_timers();
    return false;
}

void UISTPage::start_command(const std::string &command,
                             const std::list<std::string> &args,
                             const char *title, const char *error_message)
{
    stop_pty();
    pty_handle_ = pty_open(command, args);
    terminal_active_ = !pty_handle_.empty();
    if (!terminal_active_) {
        process_bytes(error_message, static_cast<int>(std::strlen(error_message)));
        render_all();
        waiting_key_to_exit_ = true;
        return;
    }
    if (title && title[0]) set_page_title(title);
    if (!start_timers()) {
        stop_pty();
        terminal_active_ = false;
        static constexpr char TIMER_ERROR[] =
            "Error: failed to start terminal timers\r\n";
        process_bytes(TIMER_ERROR, sizeof(TIMER_ERROR) - 1);
        render_all();
        waiting_key_to_exit_ = true;
        return;
    }
    render_all();
}

void UISTPage::start_shell()
{
    start_command("bash", {
        "-c",
        "cd ~ && "
        "if [ -r ~/.bashrc ]; then "
        "exec env TERM=st-256color COLORTERM=truecolor bash --rcfile ~/.bashrc -i; "
        "else "
        "exec env TERM=st-256color COLORTERM=truecolor bash -i; "
        "fi"
    }, "CLI", "cli: failed to open PTY\r\n");
}

void UISTPage::stop_pty()
{
    if (pty_handle_.empty()) return;
    cp0_signal_pty_api({"Close", pty_handle_}, nullptr);
    pty_handle_.clear();
}

int UISTPage::pty_read(char *buffer, size_t buffer_size)
{
    int result = -1;
    std::string data;
    cp0_signal_pty_api({"Read", pty_handle_, std::to_string(buffer_size)},
                       [&](int code, std::string response) {
                           result = code;
                           data = std::move(response);
                       });
    if (result < 0) return -1;
    size_t length = std::min(data.size(), buffer_size);
    if (length > 0) std::memcpy(buffer, data.data(), length);
    return static_cast<int>(length);
}

int UISTPage::pty_write(const char *buffer, size_t length)
{
    if (pty_handle_.empty() || !buffer || length == 0) return -1;
    int result = -1;
    cp0_signal_pty_api({"Write", pty_handle_, std::string(buffer, length)},
                       [&](int code, std::string) { result = code; });
    return result;
}

int UISTPage::pty_check_child(int *status)
{
    int result = -1;
    std::string data;
    cp0_signal_pty_api({"CheckChild", pty_handle_}, [&](int code, std::string response) {
        result = code;
        data = std::move(response);
    });
    if (result == 1) {
        int parsed = 0;
        if (!st_parse_child_status(data, parsed)) return -1;
        if (status) *status = parsed;
    }
    return result;
}

void UISTPage::static_poll_cb(lv_timer_t *timer) noexcept
{
    auto *self = static_cast<UISTPage *>(lv_timer_get_user_data(timer));
    if (!self) return;
    try {
        if (self->poll_timer_.current(timer)) self->poll_cb();
    } catch (...) {
        self->recover_callback_failure();
    }
}

void UISTPage::poll_cb()
{
    if (!terminal_active_ || pty_handle_.empty()) return;

    char buffer[1024];
    int length = 0;
    bool changed = false;
    while ((length = pty_read(buffer, sizeof(buffer))) > 0) {
        process_bytes(buffer, length);
        changed = true;
    }
    if (changed) {
        if (scrollback_offset_ > 0) dirty_all();
        follow_cursor_in_big_mode();
        if (big_mode_) dirty_all();
        render_all();
    }

    bool child_exited = length < 0;
    if (!child_exited && !pty_handle_.empty()) {
        int status = 0;
        child_exited = pty_check_child(&status) == 1;
    }
    if (!child_exited) return;

    terminal_active_ = false;
    stop_pty();
    static constexpr char EXIT_HINT[] = "\r\n-- Press any key to exit --";
    process_bytes(EXIT_HINT, sizeof(EXIT_HINT) - 1);
    render_all();
    waiting_key_to_exit_ = true;
}

void UISTPage::static_cursor_cb(lv_timer_t *timer) noexcept
{
    auto *self = static_cast<UISTPage *>(lv_timer_get_user_data(timer));
    if (!self) return;
    try {
        if (self->cursor_timer_.current(timer)) self->cursor_cb();
    } catch (...) {
        self->recover_callback_failure();
    }
}

void UISTPage::cursor_cb()
{
    handle_home_hold_exit();
    update_cursor();
    if (!terminal_active_ || !cursor_visible_mode_) {
        show_cursor(false);
        return;
    }
    show_cursor(!cursor_blink_visible_);
}

void UISTPage::handle_home_hold_exit()
{
    if (home_hold_status_ == 0) {
        if (cp0_esc_state_read()) {
            home_hold_status_ = 1;
            home_hold_start_ = std::chrono::steady_clock::now();
        }
        return;
    }
    if (!cp0_esc_state_read()) {
        home_hold_status_ = 0;
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - home_hold_start_).count() < 5)
        return;
    home_hold_status_ = 0;
    stop_pty();
    terminal_active_ = false;
    if (navigate_home) navigate_home();
}

void UISTPage::write_key(uint32_t evdev_key, const char *utf8)
{
    const std::string encoded =
        STKeyEncoder::encode(evdev_key, utf8, (mode_ & MODE_APPCURSOR) != 0);
    if (!encoded.empty()) pty_write(encoded.data(), encoded.size());
}
