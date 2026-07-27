#include "application.h"

#include <stdint.h>

#include "keyboard_input.h"
#include "lvgl/lvgl.h"
#include "manual_datetime_validation.h"
#include "wizard_model.h"
#include "wizard_service.h"
#include "cp0_lvgl_app_runner.hpp"
#include "ui_app_page.hpp"

#ifdef __linux__
#include <linux/input.h>
#else
#include "input_keys.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Layout / design tokens (CardputerZero OOBE Figma, 320x170 cards)
// ---------------------------------------------------------------------------
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr uint32_t kColorBg = 0x000000;
constexpr uint32_t kColorDivider = 0x121212;
constexpr uint32_t kColorBrand = 0xffffff;
constexpr uint32_t kColorMuted = 0x8f8f8f;
constexpr uint32_t kColorFieldFocusBg = 0x303030;
constexpr uint32_t kColorFieldBg = 0x151515;
constexpr uint32_t kColorFieldBorder = 0x424242;
constexpr uint32_t kColorRowSelBg = 0x202020;

// Per-screen accent colors (sampled from the Figma design).
constexpr uint32_t kAccentWelcome = 0xff6a2a;   // orange
constexpr uint32_t kAccentRegion = 0x31d843;    // green
constexpr uint32_t kAccentHostname = 0xff2aa3;  // pink
constexpr uint32_t kAccentAccount = 0x2e90ff;   // blue
constexpr uint32_t kAccentNetwork = 0xffd23e;   // yellow
constexpr uint32_t kAccentTime = 0xff5a3c;      // orange-red
constexpr uint32_t kAccentSsh = 0x2ec5ff;       // cyan
constexpr uint32_t kAccentDone = 0x31d843;      // green

using launch_wizard::Screen;
using launch_wizard::Timezone;
using launch_wizard::WifiConnectionStatus;
using launch_wizard::WifiNetwork;
using launch_wizard::kTimezoneCount;
using launch_wizard::kTimezones;
constexpr auto kWifiInitialRetryPeriod = std::chrono::seconds(2);
constexpr auto kWifiScanPeriod = std::chrono::seconds(8);
constexpr int kWifiInitialRetryCount = 4;

// ---------------------------------------------------------------------------
// Wizard model (all data collected across screens)
// ---------------------------------------------------------------------------
struct UiRuntime {
    lv_obj_t *screen_obj = nullptr;
    lv_timer_t *poll_timer = nullptr;
    lv_obj_t *config_status_label = nullptr;
    std::unique_ptr<AppPageRoot> page;
    std::thread apply_worker;
    std::thread wifi_scan_worker;
    std::thread wifi_connect_worker;
    std::thread reboot_worker;
    std::atomic<bool> cancel{false};
    std::atomic<bool> quit{false};
};

launch_wizard::WizardModel g;
UiRuntime ui;

const Timezone &current_timezone() { return g.current_timezone(); }

const lv_font_t *font_xs() { return &lv_font_montserrat_10; }
const lv_font_t *font_sm() { return &lv_font_montserrat_12; }
const lv_font_t *font_md() { return &lv_font_montserrat_14; }
const lv_font_t *font_lg() { return &lv_font_montserrat_16; }
const lv_font_t *font_xl() { return &lv_font_montserrat_22; }

lv_obj_t *add_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                    uint32_t color, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
    return label;
}

lv_obj_t *add_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg,
                   int border_w, uint32_t border_color, int radius, lv_opa_t bg_opa = LV_OPA_COVER)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, w, h);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(box, bg_opa, 0);
    if (border_w > 0) {
        lv_obj_set_style_border_color(box, lv_color_hex(border_color), 0);
        lv_obj_set_style_border_width(box, border_w, 0);
    }
    return box;
}

// SETUP mode tag + CardputerZero brand + top divider + progress bar.
void add_chrome(uint32_t accent, int progress_fill, bool show_progress = true)
{
    lv_obj_t *p = ui.screen_obj;

    // SETUP mode tag. (Figma shows a parallelogram, but the device's software
    // renderer does not draw skew-transformed rects, so use a rounded rect.)
    lv_obj_t *tag = add_rect(p, 12, 6, 82, 18, accent, 0, 0, 3);
    add_label(tag, "SETUP", font_xs(), 0xffffff, 16, 4);

    add_label(p, "CardputerZero", font_sm(), kColorBrand, 120, 7);
    add_rect(p, 8, 33, 304, 1, kColorDivider, 0, 0, 0);

    if (show_progress) {
        add_rect(p, 228, 13, 80, 4, accent, 0, 0, 2, LV_OPA_30);
        if (progress_fill < 4)
            progress_fill = 4;
        if (progress_fill > 80)
            progress_fill = 80;
        add_rect(p, 228, 13, progress_fill, 4, accent, 0, 0, 2);
    }
}

// One key hint pair, e.g. "ESC BACK".
void add_key_hint(int key_x, const char *key, int hint_x, const char *hint, uint32_t accent)
{
    add_label(ui.screen_obj, key, font_xs(), accent, key_x, 152);
    add_label(ui.screen_obj, hint, font_xs(), 0xffffff, hint_x, 152);
}

// A labeled value field (focused vs unfocused styling matches the Figma).
lv_obj_t *add_field(lv_obj_t *parent, int x, int y, int w, int h, bool focused,
                    uint32_t accent)
{
    if (focused)
        return add_rect(parent, x, y, w, h, kColorFieldFocusBg, 2, accent, 3);
    return add_rect(parent, x, y, w, h, kColorFieldBg, 1, kColorFieldBorder, 3);
}

std::size_t &text_cursor(const std::string &value)
{
    static std::unordered_map<const std::string *, std::size_t> cursors;
    auto [it, inserted] = cursors.emplace(&value, value.size());
    if (inserted || it->second > value.size())
        it->second = value.size();
    return it->second;
}

lv_obj_t *add_text_field(lv_obj_t *parent, int x, int y, int w, int h,
                         const std::string &value, bool focused, uint32_t accent,
                         bool password = false, const lv_font_t *font = nullptr)
{
    lv_obj_t *field = lv_textarea_create(parent);
    lv_obj_remove_style_all(field);
    lv_obj_set_size(field, w, h);
    lv_obj_align(field, LV_ALIGN_TOP_LEFT, x, y);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_max_length(field, 63);
    if (password) {
        lv_textarea_set_password_bullet(field, "*");
        lv_textarea_set_password_mode(field, true);
    }
    lv_textarea_set_text(field, value.c_str());
    lv_textarea_set_cursor_pos(field, static_cast<int32_t>(text_cursor(value)));

    lv_obj_set_style_text_font(field, font ? font : font_md(), 0);
    lv_obj_set_style_text_color(field, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_letter_space(field, 0, 0);
    lv_obj_set_style_bg_color(
        field, lv_color_hex(focused ? kColorFieldFocusBg : kColorFieldBg), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(
        field, lv_color_hex(focused ? accent : kColorFieldBorder), 0);
    lv_obj_set_style_border_width(field, focused ? 2 : 1, 0);
    lv_obj_set_style_radius(field, 3, 0);
    lv_obj_set_style_pad_left(field, 6, 0);
    lv_obj_set_style_pad_right(field, 6, 0);
    lv_obj_set_style_pad_top(field, 2, 0);
    lv_obj_set_style_pad_bottom(field, 2, 0);

    if (focused) {
        lv_obj_add_state(field, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(field, lv_color_hex(accent), LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_CURSOR);
    }
    return field;
}

std::string field_tail(std::string value, bool cursor, size_t max_chars)
{
    if (cursor)
        value.push_back('|');
    if (value.size() <= max_chars)
        return value;
    return value.substr(value.size() - max_chars);
}

// ===========================================================================
// Screen renderers
// ===========================================================================
void render_welcome()
{
    add_chrome(kAccentWelcome, 8);
    add_label(ui.screen_obj, "First Setup", font_xl(), 0xffffff, 34, 50);
    add_label(ui.screen_obj, "Set up CardputerZero in a few steps.", font_sm(),
              kColorMuted, 36, 100);
    add_key_hint(132, "OK", 156, "START", kAccentWelcome);
}

// Generic single-column list (timezone / wifi).
void render_list(const char *title, uint32_t accent,
                 const std::vector<std::string> &left,
                 const std::vector<std::string> &right, int sel,
                 const char *ok_hint)
{
    add_chrome(accent, 24);
    add_label(ui.screen_obj, title, font_sm(), accent, 36, 40);

    const int count = static_cast<int>(left.size());
    const int visible = 4;
    int start = sel - 1;
    if (start < 0)
        start = 0;
    if (start > count - visible)
        start = count - visible;
    if (start < 0)
        start = 0;

    const int row_y0 = 60;
    const int row_h = 24;
    for (int i = 0; i < visible && start + i < count; ++i) {
        int idx = start + i;
        int y = row_y0 + i * row_h;
        bool is_sel = idx == sel;
        if (is_sel) {
            add_rect(ui.screen_obj, 30, y - 1, 260, 22, kColorRowSelBg, 0, 0, 4);
            add_label(ui.screen_obj, ">", font_md(), accent, 42, y + 1);
        }
        add_label(ui.screen_obj, left[idx].c_str(), font_md(),
                  is_sel ? 0xffffff : kColorMuted, 66, y + 2);
        if (idx < static_cast<int>(right.size()) && !right[idx].empty()) {
            add_label(ui.screen_obj, right[idx].c_str(), font_sm(),
                      is_sel ? 0xffffff : kColorMuted, 240, y + 3);
        }
    }

    add_key_hint(14, "ESC", 38, "BACK", accent);
    add_key_hint(132, "OK", 156, ok_hint, accent);
}

void render_timezone_list()
{
    std::vector<std::string> left, right;
    for (const Timezone &t : kTimezones) {
        left.emplace_back(t.offset);
        right.emplace_back("");
    }
    render_list("TIMEZONE", kAccentRegion, left, right, g.timezone_sel, "CONFIRM");
}

void render_hostname()
{
    add_chrome(kAccentHostname, 36);
    add_label(ui.screen_obj, "HOSTNAME", font_sm(), kAccentHostname, 36, 47);
    add_text_field(ui.screen_obj, 36, 66, 248, 31, g.hostname, true,
                   kAccentHostname, false, font_lg());
    add_label(ui.screen_obj, "Default: CardputerZero", font_sm(), kColorMuted, 38, 104);

    add_key_hint(14, "ESC", 38, "BACK", kAccentHostname);
    add_key_hint(132, "OK", 156, "CONFIRM", kAccentHostname);
}

void render_account()
{
    add_chrome(kAccentAccount, 48);

    add_label(ui.screen_obj, "USERNAME", font_xs(), kAccentAccount, 37, 51);
    add_text_field(ui.screen_obj, 36, 67, 92, 24, g.username,
                   g.account_focus == 0, kAccentAccount);

    add_label(ui.screen_obj, "PASSWORD", font_xs(), kAccentAccount, 143, 51);
    add_text_field(ui.screen_obj, 142, 67, 142, 24, g.password,
                   g.account_focus == 1, kAccentAccount, !g.account_password_visible);

    add_label(ui.screen_obj, "CONFIRM PASSWORD", font_xs(), kAccentAccount, 143, 98);
    add_text_field(ui.screen_obj, 142, 110, 142, 24, g.confirm,
                   g.account_focus == 2, kAccentAccount, !g.account_password_visible);

    add_label(ui.screen_obj, "Default: pi / pi", font_sm(), kColorMuted, 36, 113);

    add_key_hint(8, "ESC", 31, "BACK", kAccentAccount);
    add_key_hint(72, "ALT", 98, g.account_password_visible ? "HIDE" : "SHOW", kAccentAccount);
    add_key_hint(145, "OK", 164, "CONFIRM", kAccentAccount);
    add_key_hint(232, "TAB", 256, "SWITCH", kAccentAccount);

    if (g.account_warning_visible) {
        add_rect(ui.screen_obj, 0, 0, kScreenWidth, kScreenHeight, 0x000000,
                 0, 0, 0, LV_OPA_60);
        lv_obj_t *dialog = add_rect(ui.screen_obj, 38, 46, 244, 88, kColorFieldBg,
                                    2, kAccentAccount, 4);
        add_label(dialog, "ACCOUNT VALIDATION", font_sm(), kAccentAccount, 14, 11);
        add_label(dialog, g.form_error.c_str(), font_sm(), 0xffffff, 14, 35);
        add_label(dialog, "OK", font_xs(), kAccentAccount, 14, 66);
        add_label(dialog, "CLOSE", font_xs(), 0xffffff, 38, 66);
    }
}

void render_pill(lv_obj_t *parent, int x, int y, int w, const char *label,
                 bool focused, uint32_t accent)
{
    add_field(parent, x, y, w, 22, focused, accent);
    add_label(parent, label, font_sm(), focused ? 0xffffff : kColorMuted, x + 8, y + 3);
}

void render_network()
{
    add_chrome(kAccentNetwork, 60);
    add_label(ui.screen_obj, "NETWORK", font_sm(), kAccentNetwork, 36, 38);
    render_pill(ui.screen_obj, 36, 56, 132, "Wi-Fi", g.network_focus == 0, kAccentNetwork);
    render_pill(ui.screen_obj, 36, 83, 132, "Ethernet", g.network_focus == 1, kAccentNetwork);
    render_pill(ui.screen_obj, 36, 110, 132, "Next", g.network_focus == 2, kAccentNetwork);

    add_key_hint(14, "ESC", 38, "BACK", kAccentNetwork);
    add_key_hint(132, "OK", 156, "CONFIRM", kAccentNetwork);
    add_key_hint(232, "TAB", 256, "SWITCH", kAccentNetwork);
}

void render_ethernet_config()
{
    add_chrome(kAccentNetwork, 64);
    add_label(ui.screen_obj, "ETH0 CONFIG", font_sm(), kAccentNetwork, 36, 38);
    render_pill(ui.screen_obj, 36, 55, 70, "DHCP", g.ethernet_dhcp, kAccentNetwork);
    render_pill(ui.screen_obj, 116, 55, 70, "STATIC", !g.ethernet_dhcp, kAccentNetwork);

    if (!g.ethernet_dhcp) {
        add_label(ui.screen_obj, "IP / CIDR", font_xs(), kAccentNetwork, 36, 81);
        add_text_field(ui.screen_obj, 36, 93, 248, 23, g.ethernet_address,
                       g.ethernet_focus == 1, kAccentNetwork);
        add_label(ui.screen_obj, "GATEWAY", font_xs(), kAccentNetwork, 36, 119);
        add_text_field(ui.screen_obj, 36, 131, 120, 20, g.ethernet_gateway,
                       g.ethernet_focus == 2, kAccentNetwork, false, font_sm());
        add_label(ui.screen_obj, "DNS", font_xs(), kAccentNetwork, 168, 119);
        add_text_field(ui.screen_obj, 168, 131, 116, 20, g.ethernet_dns,
                       g.ethernet_focus == 3, kAccentNetwork, false, font_sm());
    } else {
        add_label(ui.screen_obj, "Obtain address automatically", font_sm(),
                  kColorMuted, 36, 94);
    }

    if (!g.ethernet_error.empty())
        add_label(ui.screen_obj, g.ethernet_error.c_str(), font_xs(), 0xff5a5a, 194, 58);
    add_key_hint(8, "ESC", 31, "BACK", kAccentNetwork);
    add_key_hint(92, "Z/C", 119, "MODE", kAccentNetwork);
    add_key_hint(172, "OK", 191, "CONFIRM", kAccentNetwork);
}

void draw_signal_bars(lv_obj_t *parent, int x, int y, int signal, uint32_t accent)
{
    // 4 ascending bars; lit count based on signal strength.
    int lit = signal >= 75 ? 4 : signal >= 50 ? 3 : signal >= 25 ? 2 : 1;
    for (int i = 0; i < 4; ++i) {
        int bar_h = 4 + i * 4;
        int bar_x = x + i * 7;
        int bar_y = y + (16 - bar_h);
        uint32_t color = i < lit ? accent : kColorFieldBorder;
        add_rect(parent, bar_x, bar_y, 4, bar_h, color, 0, 0, 1);
    }
}

void render_wifi_list()
{
    add_chrome(kAccentNetwork, 60);
    if (g.wifi_status_connected) {
        const std::string connected = "Connected WiFi: " +
            field_tail(g.wifi_status_ssid, false, 10) + "  " +
            (g.wifi_status_ip.empty() ? std::string("No IP")
                                      : field_tail(g.wifi_status_ip, false, 15));
        add_label(ui.screen_obj, connected.c_str(), font_xs(), 0x31d843, 30, 41);
    } else {
        add_label(ui.screen_obj, "SELECT WI-FI", font_sm(), kAccentNetwork, 36, 40);
    }

    // #94: while the first scan is still running show a loading state with a
    // spinner so the user isn't staring at an empty network list.
    if (g.wifi_scanning && g.wifi_list.empty()) {
        lv_obj_t *spinner = lv_spinner_create(ui.screen_obj);
        lv_obj_set_size(spinner, 28, 28);
        lv_spinner_set_anim_params(spinner, 1000, 60);
        lv_obj_align(spinner, LV_ALIGN_TOP_LEFT, 40, 78);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(kColorFieldBorder), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(kAccentNetwork), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
        add_label(ui.screen_obj, "Scanning for networks...", font_sm(), kColorMuted, 80, 86);
        add_key_hint(14, "ESC", 38, "BACK", kAccentNetwork);
        add_key_hint(112, "ALT", 136, "ADD HIDDEN WI-FI", kAccentNetwork);
        return;
    }

    const int count = static_cast<int>(g.wifi_list.size());
    const int visible = 4;
    int start = g.wifi_sel - 1;
    if (start < 0)
        start = 0;
    if (start > count - visible)
        start = count - visible;
    if (start < 0)
        start = 0;

    const int row_y0 = 57;
    const int row_h = 23;
    for (int i = 0; i < visible && start + i < count; ++i) {
        int idx = start + i;
        int y = row_y0 + i * row_h;
        bool is_sel = idx == g.wifi_sel;
        if (is_sel) {
            add_rect(ui.screen_obj, 30, y, 260, 21, kColorRowSelBg, 0, 0, 4);
            add_label(ui.screen_obj, ">", font_md(), kAccentNetwork, 42, y + 1);
        }
        // Keep a comfortable gap before the signal indicator. The focused
        // SSID scrolls so its full name remains inspectable.
        lv_obj_t *ssid_label = add_label(
            ui.screen_obj, g.wifi_list[idx].ssid.c_str(), font_sm(),
            is_sel ? 0xffffff : kColorMuted, 66, y + 4);
        lv_obj_set_size(ssid_label, 176, 16);
        lv_label_set_long_mode(
            ssid_label,
            is_sel ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
        draw_signal_bars(ui.screen_obj, 252, y + 1, g.wifi_list[idx].signal, kAccentNetwork);
    }

    add_key_hint(14, "ESC", 38, "BACK", kAccentNetwork);
    add_key_hint(84, "ALT", 108, "ADD HIDDEN WI-FI", kAccentNetwork);
    if (count > 0)
        add_key_hint(230, "OK", 250, "SELECT", kAccentNetwork);
}

void render_wifi_password()
{
    add_chrome(kAccentNetwork, 60);
    if (g.wifi_connected) {
        add_label(ui.screen_obj, "WI-FI CONNECTED", font_sm(), 0x31d843, 36, 48);
        const std::string status = "Connected WiFi: " + field_tail(g.wifi_ssid, false, 24);
        add_label(ui.screen_obj, status.c_str(), font_sm(), 0xffffff, 36, 76);
        const std::string ip = "IP: " + (g.wifi_ip.empty() ? std::string("Unavailable") : g.wifi_ip);
        add_label(ui.screen_obj, ip.c_str(), font_sm(), kColorMuted, 36, 99);
        add_key_hint(14, "ESC", 38, "BACK", kAccentNetwork);
        add_key_hint(208, "OK", 232, "NEXT", kAccentNetwork);
        return;
    }

    if (g.wifi_manual) {
        add_label(ui.screen_obj, "SSID", font_xs(), kAccentNetwork, 36, 43);
        add_text_field(ui.screen_obj, 36, 57, 248, 25, g.wifi_ssid,
                       g.wifi_focus == 0, kAccentNetwork);

        add_label(ui.screen_obj, "PASSWORD", font_xs(), kAccentNetwork, 36, 86);
        add_text_field(ui.screen_obj, 36, 100, 248, 25, g.wifi_password,
                       g.wifi_focus == 1, kAccentNetwork, !g.wifi_password_visible);
    } else {
        add_label(ui.screen_obj, "WI-FI PASSWORD", font_sm(), kAccentNetwork, 36, 47);
        add_text_field(ui.screen_obj, 36, 66, 248, 31, g.wifi_password, true,
                       kAccentNetwork, !g.wifi_password_visible, font_lg());
        const std::string note = "SSID: " + field_tail(g.wifi_ssid, false, 28);
        add_label(ui.screen_obj, note.c_str(), font_sm(), kColorMuted, 38, 104);
    }

    if (g.wifi_connecting)
        add_label(ui.screen_obj, "Connecting...", font_sm(), kAccentNetwork, 38, 128);
    else if (!g.wifi_connect_error.empty())
        add_label(ui.screen_obj, g.wifi_connect_error.c_str(), font_sm(), 0xff5a5a, 38, 128);

    add_key_hint(8, "ESC", 31, "BACK", kAccentNetwork);
    add_key_hint(72, "ALT", 98, g.wifi_password_visible ? "HIDE" : "SHOW", kAccentNetwork);
    add_key_hint(145, "OK", 164, "CONNECT", kAccentNetwork);
    add_key_hint(244, "TAB", 268, g.wifi_manual ? "SWITCH" : "NEXT", kAccentNetwork);
}

void render_manual_time()
{
    add_chrome(kAccentTime, 60);
    add_label(ui.screen_obj, "MANUAL TIME", font_sm(), kAccentTime, 36, 41);
    add_label(ui.screen_obj, "Review date and time before", font_sm(), kColorMuted, 36, 57);
    add_label(ui.screen_obj, "continuing setup.", font_sm(), kColorMuted, 36, 69);

    add_text_field(ui.screen_obj, 36, 89, 126, 26, g.manual_date,
                   g.time_focus == 0, kAccentTime);

    add_text_field(ui.screen_obj, 174, 89, 84, 26, g.manual_time,
                   g.time_focus == 1, kAccentTime);

    add_key_hint(14, "ESC", 38, "BACK", kAccentTime);
    add_key_hint(132, "OK", 156, "CONFIRM", kAccentTime);
    add_key_hint(232, "TAB", 256, "SWITCH", kAccentTime);

    if (g.time_warning_visible) {
        add_rect(ui.screen_obj, 0, 0, kScreenWidth, kScreenHeight, 0x000000,
                 0, 0, 0, LV_OPA_60);
        lv_obj_t *dialog = add_rect(ui.screen_obj, 38, 46, 244, 88, kColorFieldBg,
                                    2, kAccentTime, 4);
        add_label(dialog, "INVALID DATE OR TIME", font_sm(), kAccentTime, 14, 11);
        add_label(dialog, g.time_warning_message.c_str(), font_sm(), 0xffffff, 14, 35);
        add_label(dialog, "OK", font_xs(), kAccentTime, 14, 66);
        add_label(dialog, "CLOSE", font_xs(), 0xffffff, 38, 66);
    }
}

void render_ssh()
{
    add_chrome(kAccentSsh, 70);
    add_label(ui.screen_obj, "Enable SSH", font_sm(), kAccentSsh, 36, 41);
    render_pill(ui.screen_obj, 36, 85, 70, "YES", g.ssh_focus == 0, kAccentSsh);
    render_pill(ui.screen_obj, 118, 85, 70, "NO", g.ssh_focus == 1, kAccentSsh);

    add_key_hint(14, "ESC", 38, "BACK", kAccentSsh);
    add_key_hint(132, "OK", 156, "CONFIRM", kAccentSsh);
    add_key_hint(232, "TAB", 256, "SWITCH", kAccentSsh);
}

void set_loading_arc_rotation(void *object, int32_t rotation)
{
    lv_arc_set_rotation(static_cast<lv_obj_t *>(object), rotation);
}

void render_applying()
{
    add_chrome(kAccentDone, 80, false);
    std::string message;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        message = g.worker_message.empty() ? "Configuring..." : g.worker_message;
    }
    lv_obj_t *title = add_label(
        ui.screen_obj, "Configuring device", font_lg(), 0xffffff, 0, 42);
    lv_obj_set_width(title, kScreenWidth);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *loading_arc = lv_arc_create(ui.screen_obj);
    lv_obj_remove_style(loading_arc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(loading_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(loading_arc, 34, 34);
    lv_obj_align(loading_arc, LV_ALIGN_TOP_MID, 0, 67);
    lv_arc_set_bg_angles(loading_arc, 0, 360);
    lv_arc_set_angles(loading_arc, 0, 96);
    lv_obj_set_style_arc_width(loading_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(loading_arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(
        loading_arc, lv_color_hex(kColorFieldBorder), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(loading_arc, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_arc_color(
        loading_arc, lv_color_hex(kAccentDone), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(loading_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(loading_arc, true, LV_PART_INDICATOR);

    lv_anim_t rotation;
    lv_anim_init(&rotation);
    lv_anim_set_var(&rotation, loading_arc);
    lv_anim_set_exec_cb(&rotation, set_loading_arc_rotation);
    lv_anim_set_values(&rotation, 0, 360);
    lv_anim_set_duration(&rotation, 1150);
    lv_anim_set_path_cb(&rotation, lv_anim_path_linear);
    lv_anim_set_repeat_count(&rotation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&rotation);

    ui.config_status_label = add_label(
        ui.screen_obj, message.c_str(), font_sm(), kColorMuted, 24, 113);
    lv_label_set_long_mode(ui.config_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ui.config_status_label, 272);
    lv_obj_set_style_text_align(ui.config_status_label, LV_TEXT_ALIGN_CENTER, 0);
}

void render_restart_prompt()
{
    add_chrome(kAccentDone, 80, false);
    lv_obj_t *dialog = add_rect(ui.screen_obj, 30, 38, 260, 102, kColorFieldBg, 2, kAccentDone, 4);
    add_label(dialog, "RESTART REQUIRED", font_lg(), kAccentDone, 17, 13);
    add_label(dialog, "Device needs to restart", font_sm(), 0xffffff, 17, 46);
    add_label(dialog, "OK", font_sm(), kAccentDone, 17, 73);
}

void render_restart_or_error(bool restarting)
{
    add_chrome(restarting ? kAccentDone : kAccentTime, 80, false);
    std::string message;
    { std::lock_guard<std::mutex> lock(g.mutex); message = g.worker_message; }
    add_label(ui.screen_obj, restarting ? "Restarting device..." : "Configuration failed",
              font_lg(), 0xffffff, 36, 54);
    lv_obj_t *label = add_label(ui.screen_obj, message.c_str(), font_sm(),
                                restarting ? kColorMuted : 0xff5a5a, 36, 84);
    lv_obj_set_width(label, 248);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    if (!restarting) {
        add_key_hint(14, "ESC", 38, "BACK", kAccentTime);
        add_key_hint(132, "OK", 156, "RETRY", kAccentTime);
    }
}

void render()
{
    ui.config_status_label = nullptr;
    lv_obj_clean(ui.screen_obj);
    switch (g.screen) {
    case Screen::Welcome: render_welcome(); break;
    case Screen::TimezoneList: render_timezone_list(); break;
    case Screen::Hostname: render_hostname(); break;
    case Screen::Account: render_account(); break;
    case Screen::Network: render_network(); break;
    case Screen::EthernetConfig: render_ethernet_config(); break;
    case Screen::WifiList: render_wifi_list(); break;
    case Screen::WifiPassword: render_wifi_password(); break;
    case Screen::ManualTime: render_manual_time(); break;
    case Screen::Ssh: render_ssh(); break;
    case Screen::Applying: render_applying(); break;
    case Screen::RestartPrompt: render_restart_prompt(); break;
    case Screen::Restarting: render_restart_or_error(true); break;
    case Screen::ApplyError: render_restart_or_error(false); break;
    }
}

void go(Screen screen)
{
    g.screen = screen;
    render();
}

// ===========================================================================
// Apply orchestration
// ===========================================================================
void start_apply()
{
    if (g.busy)
        return;
    g.busy = true;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        g.worker_message = "Configuring...";
    }
    go(Screen::Applying);

    if (ui.apply_worker.joinable()) ui.apply_worker.join();
    ui.apply_worker = std::thread([]() {
        std::string message = launch_wizard::WizardService::apply(g, [](const std::string &status) {
            { std::lock_guard<std::mutex> lock(g.mutex); g.worker_message = status; }
            cp0_lvgl_wake();
            return !ui.cancel.load();
        });
        std::lock_guard<std::mutex> lock(g.mutex);
        g.worker_message = message;
        g.worker_finished = true;
        g.succeeded = message.empty();
        cp0_lvgl_wake();
    });
}

// ===========================================================================
// Input handling
// ===========================================================================
std::string *active_text_field()
{
    switch (g.screen) {
    case Screen::Hostname:
        return &g.hostname;
    case Screen::Account:
        return g.account_focus == 0 ? &g.username
             : g.account_focus == 1 ? &g.password
                                    : &g.confirm;
    case Screen::WifiPassword:
        if (g.wifi_connected)
            return nullptr;
        return g.wifi_manual && g.wifi_focus == 0 ? &g.wifi_ssid : &g.wifi_password;
    case Screen::EthernetConfig:
        if (g.ethernet_dhcp || g.ethernet_focus == 0)
            return nullptr;
        if (g.ethernet_focus == 1)
            return &g.ethernet_address;
        return g.ethernet_focus == 2 ? &g.ethernet_gateway : &g.ethernet_dns;
    case Screen::ManualTime:
        return g.time_focus == 0 ? &g.manual_date : &g.manual_time;
    default:
        return nullptr;
    }
}

void handle_text_char(char ch)
{
    if (g.screen == Screen::WifiPassword && (g.wifi_connecting || g.wifi_connected))
        return;
    std::string *field = active_text_field();
    if (!field)
        return;
    if (field->size() >= 63)
        return;
    std::size_t &cursor = text_cursor(*field);
    field->insert(field->begin() + static_cast<std::ptrdiff_t>(cursor), ch);
    ++cursor;
    g.form_error.clear();
    g.wifi_connect_error.clear();
    render();
}

void handle_backspace()
{
    if (g.screen == Screen::WifiPassword && (g.wifi_connecting || g.wifi_connected))
        return;
    std::string *field = active_text_field();
    if (field && !field->empty()) {
        std::size_t &cursor = text_cursor(*field);
        if (cursor == 0)
            return;
        field->erase(cursor - 1, 1);
        --cursor;
        g.form_error.clear();
        g.wifi_connect_error.clear();
        render();
    }
}

void move_text_cursor(int delta)
{
    std::string *field = active_text_field();
    if (!field)
        return;
    std::size_t &cursor = text_cursor(*field);
    if (delta < 0) {
        if (cursor > 0)
            --cursor;
    } else if (cursor < field->size()) {
        ++cursor;
    }
    render();
}

void enter_wifi_list()
{
    // #94: scan asynchronously so the list screen appears immediately with a
    // loading spinner and refreshes as soon as results arrive (poll_worker_cb).
    g.wifi_list.clear();
    g.wifi_sel = 0;
    uint64_t scan_generation = 0;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        g.wifi_scan_ready = false;
        g.wifi_scan_result.clear();
        scan_generation = ++g.wifi_scan_generation;
    }
    g.wifi_scanning = true;
    go(Screen::WifiList);

    if (ui.wifi_scan_worker.joinable()) ui.wifi_scan_worker.join();
    ui.wifi_scan_worker = std::thread([scan_generation]() {
        int scan_count = 0;
        for (;;) {
            std::vector<WifiNetwork> networks =
                launch_wizard::WizardService::scan_wifi();
            const WifiConnectionStatus connection =
                launch_wizard::WizardService::read_wifi_status();
            {
                std::lock_guard<std::mutex> lock(g.mutex);
                if (g.wifi_scan_generation != scan_generation)
                    return;
                g.wifi_scan_result = networks;
                g.wifi_scan_status = connection;
                g.wifi_scan_ready = true;
            }
            cp0_lvgl_wake();
            if (!networks.empty())
                return;

            ++scan_count;
            const auto delay = scan_count < kWifiInitialRetryCount
                ? kWifiInitialRetryPeriod : kWifiScanPeriod;
            const auto deadline = std::chrono::steady_clock::now() + delay;
            while (std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::lock_guard<std::mutex> lock(g.mutex);
                if (ui.cancel.load() || g.wifi_scan_generation != scan_generation)
                    return;
            }
        }
    });
}

void cancel_wifi_scan()
{
    std::lock_guard<std::mutex> lock(g.mutex);
    ++g.wifi_scan_generation;
    g.wifi_scan_ready = false;
    g.wifi_scan_result.clear();
    g.wifi_scanning = false;
}

void enter_hidden_wifi()
{
    cancel_wifi_scan();
    g.wifi_ssid.clear();
    g.wifi_password.clear();
    g.wifi_manual = true;
    g.wifi_hidden = true;
    g.wifi_focus = 0;
    g.wifi_password_visible = false;
    g.wifi_ip.clear();
    g.wifi_connect_error.clear();
    g.wifi_connected = false;
    go(Screen::WifiPassword);
}

void start_wifi_connection()
{
    if (g.wifi_connecting)
        return;
    std::string validation_error;
    if (!launch_wizard::validate_wifi_ssid(g.wifi_ssid, validation_error)) {
        g.wifi_connect_error = validation_error;
        render();
        return;
    }

    const std::string ssid = g.wifi_ssid;
    const std::string password = g.wifi_password;
    const bool hidden = g.wifi_hidden;
    g.wifi_connecting = true;
    g.wifi_connected = false;
    g.wifi_ip.clear();
    g.wifi_connect_error.clear();
    render();

    if (ui.wifi_connect_worker.joinable()) ui.wifi_connect_worker.join();
    ui.wifi_connect_worker = std::thread([ssid, password, hidden]() {
        std::string connected_ip;
        const std::string error = launch_wizard::WizardService::connect_wifi(
            ssid, password, &connected_ip, hidden);
        std::lock_guard<std::mutex> lock(g.mutex);
        g.wifi_connect_succeeded = error.empty();
        g.wifi_connect_error = error;
        g.wifi_ip = error.empty() ? connected_ip : std::string();
        if (error.empty()) {
            g.wifi_status_connected = true;
            g.wifi_status_ssid = ssid;
            g.wifi_status_ip = connected_ip;
        }
        g.wifi_connect_ready = true;
        cp0_lvgl_wake();
    });
}

// ESC navigation per screen.
void handle_back()
{
    switch (g.screen) {
    case Screen::Welcome: break;
    case Screen::TimezoneList: go(Screen::Welcome); break;
    case Screen::Hostname: go(Screen::TimezoneList); break;
    case Screen::Account: go(Screen::Hostname); break;
    case Screen::Network: go(Screen::Account); break;
    case Screen::EthernetConfig: go(Screen::Network); break;
    case Screen::WifiList:
        cancel_wifi_scan();
        go(Screen::Network);
        break;
    case Screen::WifiPassword:
        if (!g.wifi_connecting)
            go(Screen::WifiList);
        break;
    case Screen::ManualTime:
        go(g.network_skipped ? Screen::Network
                             : (g.use_ethernet ? Screen::EthernetConfig
                                               : Screen::WifiPassword));
        break;
    case Screen::Ssh: go(Screen::ManualTime); break;
    case Screen::ApplyError: go(Screen::Ssh); break;
    case Screen::Applying: break;
    case Screen::RestartPrompt: break;
    case Screen::Restarting: break;
    }
}

void move_focus(int delta)
{
    switch (g.screen) {
    case Screen::TimezoneList: {
        int n = kTimezoneCount;
        g.timezone_sel = (g.timezone_sel + delta + n) % n;
        render();
        break;
    }
    case Screen::Account:
        g.account_focus = (g.account_focus + delta + 3) % 3;
        g.form_error.clear();
        render();
        break;
    case Screen::Network:
        g.network_focus = (g.network_focus + delta + 3) % 3;
        render();
        break;
    case Screen::EthernetConfig:
        if (!g.ethernet_dhcp) {
            g.ethernet_focus = (g.ethernet_focus + delta + 4) % 4;
            g.ethernet_error.clear();
            render();
        }
        break;
    case Screen::WifiList: {
        int n = static_cast<int>(g.wifi_list.size());
        if (n == 0)
            break;
        g.wifi_sel = (g.wifi_sel + delta + n) % n;
        render();
        break;
    }
    case Screen::WifiPassword:
        if (g.wifi_manual && !g.wifi_connecting && !g.wifi_connected) {
            g.wifi_focus = (g.wifi_focus + delta + 2) % 2;
            g.wifi_connect_error.clear();
            render();
        }
        break;
    case Screen::ManualTime:
        g.time_focus = (g.time_focus + delta + 2) % 2;
        render();
        break;
    case Screen::Ssh:
        g.ssh_focus = (g.ssh_focus + delta + 2) % 2;
        render();
        break;
    default:
        break;
    }
}

void confirm_manual_time()
{
    std::string error;
    if (!launch_wizard::validate_manual_datetime(g.manual_date, g.manual_time, error)) {
        g.time_warning_message = error;
        g.time_warning_visible = true;
        render();
        return;
    }
    g.time_warning_visible = false;
    g.time_warning_message.clear();
    go(Screen::Ssh);
}

bool validate_account_fields()
{
    if (g.username.empty())
        g.username = "pi";
    if (g.password.empty() && g.confirm.empty()) {
        g.password = "pi";
        g.confirm = "pi";
    }

    std::string error;
    if (!launch_wizard::validate_username(g.username, error) ||
        !launch_wizard::validate_password(g.password, error)) {
        g.form_error = error;
        g.account_warning_visible = true;
        render();
        return false;
    }
    if (g.password != g.confirm) {
        g.form_error = "Passwords do not match";
        g.account_warning_visible = true;
        render();
        return false;
    }

    g.form_error.clear();
    g.account_warning_visible = false;
    return true;
}

void handle_enter()
{
    switch (g.screen) {
    case Screen::Welcome:
        g.timezone_sel = g.timezone_index;
        go(Screen::TimezoneList);
        break;
    case Screen::TimezoneList:
        g.timezone_index = g.timezone_sel;
        go(Screen::Hostname);
        break;
    case Screen::Hostname: {
        std::string error;
        if (g.hostname.empty())
            g.hostname = "CardputerZero";
        if (!launch_wizard::validate_hostname(g.hostname, error))
            return;
        g.account_password_visible = false;
        go(Screen::Account);
        break;
    }
    case Screen::Account:
        if (!validate_account_fields())
            return;
        go(Screen::Network);
        break;
    case Screen::Network:
        if (g.network_focus == 0) {
            g.use_ethernet = false;
            g.network_skipped = false;
            enter_wifi_list();
        } else if (g.network_focus == 1) {
            g.use_ethernet = true;
            g.network_skipped = false;
            g.ethernet_focus = 0;
            g.ethernet_error.clear();
            go(Screen::EthernetConfig);
        } else {
            g.network_skipped = true;
            g.use_ethernet = false;
            go(Screen::ManualTime);
        }
        break;
    case Screen::EthernetConfig:
        if (!g.ethernet_dhcp && g.ethernet_focus < 3) {
            ++g.ethernet_focus;
            render();
        } else {
            g.ethernet_error = launch_wizard::validate_ethernet_config(g);
            if (!g.ethernet_error.empty()) {
                render();
                return;
            }
            go(Screen::ManualTime);
        }
        break;
    case Screen::WifiList:
        if (g.wifi_list.empty())
            break;
        cancel_wifi_scan();
        g.wifi_ssid = g.wifi_list[g.wifi_sel].ssid;
        g.wifi_manual = false;
        g.wifi_hidden = false;
        g.wifi_focus = 1;
        g.wifi_password.clear();
        g.wifi_password_visible = false;
        g.wifi_ip.clear();
        g.wifi_connect_error.clear();
        g.wifi_connected = false;
        go(Screen::WifiPassword);
        break;
    case Screen::WifiPassword:
        if (g.wifi_connected) {
            go(Screen::ManualTime);
        } else if (g.wifi_manual && g.wifi_focus == 0) {
            g.wifi_focus = 1;
            render();
        } else {
            start_wifi_connection();
        }
        break;
    case Screen::ManualTime:
        confirm_manual_time();
        break;
    case Screen::Ssh:
        g.ssh_enabled = g.ssh_focus == 0;
        start_apply();
        break;
    case Screen::Applying:
    case Screen::Restarting:
        break;
    case Screen::RestartPrompt:
        if (ui.reboot_worker.joinable()) ui.reboot_worker.join();
        go(Screen::Restarting);
        ui.reboot_worker = std::thread([] {
            const std::string error = launch_wizard::WizardService::reboot();
            {
                std::lock_guard<std::mutex> lock(g.mutex);
                g.worker_message = error;
                g.worker_finished = true;
                g.succeeded = error.empty();
            }
            cp0_lvgl_wake();
        });
        break;
    case Screen::ApplyError:
        start_apply();
        break;
    }
}

// TAB only moves focus within forms that explicitly use it for switching.
void handle_tab()
{
    switch (g.screen) {
    case Screen::Account:
        g.account_focus = (g.account_focus + 1) % 3;
        g.form_error.clear();
        render();
        break;
    case Screen::Network:
        g.network_focus = (g.network_focus + 1) % 3;
        render();
        break;
    case Screen::EthernetConfig:
        g.ethernet_error = launch_wizard::validate_ethernet_config(g);
        if (!g.ethernet_error.empty()) {
            render();
            return;
        }
        go(Screen::ManualTime);
        break;
    case Screen::WifiPassword:
        if (g.wifi_manual && !g.wifi_connecting && !g.wifi_connected) {
            g.wifi_focus = (g.wifi_focus + 1) % 2;
            g.wifi_connect_error.clear();
            render();
        } else {
            handle_enter();
        }
        break;
    case Screen::ManualTime:
        g.time_focus = (g.time_focus + 1) % 2;
        render();
        break;
    case Screen::Ssh:
        g.ssh_focus = (g.ssh_focus + 1) % 2;
        render();
        break;
    default:
        break;
    }
}

void keyboard_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD))
        return;

    auto *key = static_cast<key_item *>(lv_event_get_param(event));
    if (!key || key->key_state == KBD_KEY_RELEASED)
        return;
    if (g.busy)
        return;

    if (g.account_warning_visible) {
        switch (key->key_code) {
        case KEY_ENTER:
        case KEY_ESC:
        case KEY_TAB:
            g.account_warning_visible = false;
            g.form_error.clear();
            render();
            break;
        default:
            break;
        }
        return;
    }

    if (g.time_warning_visible) {
        switch (key->key_code) {
        case KEY_ENTER:
        case KEY_ESC:
        case KEY_TAB:
            g.time_warning_visible = false;
            render();
            break;
        default:
            break;
        }
        return;
    }

    uint32_t key_code = key->key_code;
    if (g.screen == Screen::WifiList && key_code == KEY_LEFTALT &&
        key->key_state == KBD_KEY_PRESSED) {
        enter_hidden_wifi();
        return;
    }
    if (g.screen == Screen::Account && key_code == KEY_LEFTALT &&
        key->key_state == KBD_KEY_PRESSED) {
        g.account_password_visible = !g.account_password_visible;
        render();
        return;
    }
    if (g.screen == Screen::WifiPassword && key_code == KEY_LEFTALT &&
        key->key_state == KBD_KEY_PRESSED && !g.wifi_connecting && !g.wifi_connected) {
        g.wifi_password_visible = !g.wifi_password_visible;
        render();
        return;
    }
    if (g.screen == Screen::TimezoneList || g.screen == Screen::WifiList) {
        switch (key_code) {
        case KEY_F: key_code = KEY_UP; break;
        case KEY_X: key_code = KEY_DOWN; break;
        case KEY_Z: key_code = KEY_LEFT; break;
        case KEY_C: key_code = KEY_RIGHT; break;
        default: break;
        }
    } else if (g.screen == Screen::Network) {
        if (key_code == KEY_F)
            key_code = KEY_UP;
        else if (key_code == KEY_X)
            key_code = KEY_DOWN;
    } else if (g.screen == Screen::EthernetConfig) {
        if (g.ethernet_focus == 0 &&
            (key_code == KEY_LEFT || key_code == KEY_Z ||
             key_code == KEY_RIGHT || key_code == KEY_C)) {
            g.ethernet_dhcp = key_code == KEY_LEFT || key_code == KEY_Z;
            g.ethernet_focus = 0;
            g.ethernet_error.clear();
            render();
            return;
        }
        if (key_code == KEY_F)
            key_code = KEY_UP;
        else if (key_code == KEY_X)
            key_code = KEY_DOWN;
    } else if (g.screen == Screen::Ssh) {
        if (key_code == KEY_Z)
            key_code = KEY_LEFT;
        else if (key_code == KEY_C)
            key_code = KEY_RIGHT;
    }

    switch (key_code) {
    case KEY_ESC:
        handle_back();
        return;
    case KEY_TAB:
        handle_tab();
        return;
    case KEY_UP:
        move_focus(-1);
        return;
    case KEY_DOWN:
        move_focus(1);
        return;
    case KEY_LEFT:
        if (active_text_field())
            move_text_cursor(-1);
        else
            move_focus(-1);
        return;
    case KEY_RIGHT:
        if (active_text_field())
            move_text_cursor(1);
        else
            move_focus(1);
        return;
    case KEY_ENTER:
        handle_enter();
        return;
    case KEY_BACKSPACE:
    case KEY_DELETE:
        handle_backspace();
        return;
    default:
        break;
    }

    if (key->utf8[0] != '\0' && key->utf8[1] == '\0') {
        unsigned char ch = static_cast<unsigned char>(key->utf8[0]);
        if (ch >= 0x20 && ch < 0x7f)
            handle_text_char(static_cast<char>(ch));
    }
}

void poll_worker_cb(lv_timer_t *timer)
{
    (void)timer;

    // #94: pull in async Wi-Fi scan results and refresh the list live.
    bool wifi_updated = false;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        if (g.wifi_scan_ready) {
            g.wifi_scan_ready = false;
            g.wifi_list = std::move(g.wifi_scan_result);
            g.wifi_scan_result.clear();
            if (g.wifi_scan_status.available) {
                g.wifi_status_connected = g.wifi_scan_status.connected;
                g.wifi_connected = g.wifi_scan_status.connected;
                g.wifi_status_ssid = g.wifi_scan_status.ssid;
                g.wifi_status_ip = g.wifi_scan_status.ip;
            }
            g.wifi_scanning = false;
            wifi_updated = true;
        }
    }
    if (wifi_updated && g.screen == Screen::WifiList) {
        if (g.wifi_sel >= static_cast<int>(g.wifi_list.size()))
            g.wifi_sel = 0;
        render();
    }

    bool wifi_connect_finished = false;
    bool wifi_connect_succeeded = false;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        if (g.wifi_connect_ready) {
            g.wifi_connect_ready = false;
            wifi_connect_finished = true;
            wifi_connect_succeeded = g.wifi_connect_succeeded;
        }
    }
    if (wifi_connect_finished) {
        g.wifi_connecting = false;
        g.wifi_connected = wifi_connect_succeeded;
        if (g.screen == Screen::WifiPassword)
            render();
    }

    bool finished = false;
    bool succeeded = false;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        if (g.worker_finished) {
            g.worker_finished = false;
            finished = true;
            succeeded = g.succeeded;
        }
    }

    if (finished) {
        g.busy = false;
        if (g.screen == Screen::Applying) {
            go(succeeded ? Screen::RestartPrompt : Screen::ApplyError);
        } else if (g.screen == Screen::Restarting) {
            if (succeeded) ui.quit.store(true);
            else go(Screen::RestartPrompt);
        }
    } else if (g.screen == Screen::Applying && ui.config_status_label) {
        std::string message;
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            message = g.worker_message.empty() ? "Configuring..." : g.worker_message;
        }
        if (strcmp(lv_label_get_text(ui.config_status_label), message.c_str()) != 0)
            lv_label_set_text(ui.config_status_label, message.c_str());
    }
}

void build_ui()
{
    ui.screen_obj = ui.page->screen();
    lv_obj_remove_style_all(ui.screen_obj);
    lv_obj_clear_flag(ui.screen_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui.screen_obj, kScreenWidth, kScreenHeight);
    lv_obj_set_style_bg_color(ui.screen_obj, lv_color_hex(kColorBg), 0);
    lv_obj_set_style_bg_opa(ui.screen_obj, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(ui.screen_obj, keyboard_event_cb,
                        static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), nullptr);

    ui.poll_timer = lv_timer_create(poll_worker_cb, 200, nullptr);
    render();
}

}  // namespace

bool launch_wizard_should_run(void)
{
    return launch_wizard::WizardService::should_run();
}

int launch_wizard_finish_configured_system(void)
{
    return launch_wizard::WizardService::finish_configured_system();
}

void launch_wizard_register_event(void)
{
    if (LV_EVENT_KEYBOARD == 0) LV_EVENT_KEYBOARD = lv_event_register_id();
}

bool launch_wizard_ui_setup(void)
{
    cp0_keyboard_set_lvgl_keypad_intercept(0);
    ui.cancel.store(false);
    ui.quit.store(false);
    ui.page = std::make_unique<AppPageRoot>();
    cp0_lvgl_start_app_page(*ui.page);
    ui.page->disable_top_bar();
    build_ui();
    const char *preview = getenv("LAUNCH_WIZARD_PREVIEW");
    if (preview && strcmp(preview, "configuring") == 0) {
        g.screen = Screen::Applying;
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            g.worker_message = "Setting date and time...";
        }
        render();
    } else if (preview && strcmp(preview, "restart") == 0) {
        go(Screen::RestartPrompt);
    }
    const bool ready = ui.screen_obj != nullptr;
    cp0_keyboard_set_lvgl_keypad_intercept(ready);
    return ready;
}

bool launch_wizard_should_quit(void)
{
    return ui.quit.load();
}

void launch_wizard_ui_teardown(void)
{
    cp0_keyboard_set_lvgl_keypad_intercept(0);
    ui.cancel.store(true);
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        ++g.wifi_scan_generation;
    }
    cp0_lvgl_wake();
    if (ui.wifi_scan_worker.joinable()) ui.wifi_scan_worker.join();
    if (ui.wifi_connect_worker.joinable()) ui.wifi_connect_worker.join();
    if (ui.apply_worker.joinable()) ui.apply_worker.join();
    if (ui.reboot_worker.joinable()) ui.reboot_worker.join();
    if (ui.poll_timer) {
        lv_timer_delete(ui.poll_timer);
        ui.poll_timer = nullptr;
    }
    ui.screen_obj = nullptr;
    ui.config_status_label = nullptr;
    ui.page.reset();
}
