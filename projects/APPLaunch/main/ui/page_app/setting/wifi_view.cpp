#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"
#include "keyboard_input.h"

namespace setting {

namespace {

lv_obj_t *create_label(lv_obj_t *parent, const char *text, int x, int y, uint32_t color,
                       const lv_font_t *font)
{
    if (!parent) return nullptr;
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    return label;
}

lv_obj_t *begin_view(lv_obj_t *container)
{
    if (!container) return nullptr;
    lv_obj_t *view = lv_obj_create(container);
    if (!view) return nullptr;
    lv_obj_set_size(view, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(view, 0, 0);
    lv_obj_set_style_radius(view, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(view, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(view, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(view, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
    return view;
}

void commit_view(lv_obj_t *container, lv_obj_t *view)
{
    while (lv_obj_get_child_count(container) > 1) {
        lv_obj_t *child = lv_obj_get_child(container, 0);
        if (child == view) child = lv_obj_get_child(container, 1);
        lv_obj_delete(child);
    }
    lv_obj_remove_flag(view, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

WiFiListView::~WiFiListView()
{
    unmount();
}

void WiFiListView::reset_objects()
{
    root_ = nullptr;
    title_ = nullptr;
    empty_ = nullptr;
    hint_ = nullptr;
    title_text_.clear();
    empty_text_.clear();
    hint_text_.clear();
    for (auto &row : rows_) row = {};
}

void WiFiListView::root_delete_cb(lv_event_t *event) noexcept
{
    if (!event || lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    auto *view = static_cast<WiFiListView *>(lv_event_get_user_data(event));
    if (view && lv_event_get_target(event) == view->root_) view->reset_objects();
}

void WiFiListView::unmount()
{
    if (root_) lv_obj_delete(root_);
    reset_objects();
}

bool WiFiListView::mount(UISetupPage &page)
{
    if (mounted()) return true;
    SetupPageAccess access(page);
    lv_obj_t *container = access.content_container();
    lv_obj_t *view = begin_view(container);
    if (!view) return false;
    root_ = view;
    lv_obj_add_event_cb(view, root_delete_cb, LV_EVENT_DELETE, this);
    auto fail = [&] {
        if (root_) lv_obj_delete(root_);
        reset_objects();
        return false;
    };

    title_ = create_label(
        view, "", 8, 2, 0x58A6FF,
        launcher_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD));
    if (!title_) return fail();

    if (!create_label(view, "SSID", 8, 18, 0x888888, &lv_font_montserrat_10) ||
        !create_label(view, "Security", 180, 18, 0x888888, &lv_font_montserrat_10) ||
        !create_label(view, "Signal", 270, 18, 0x888888, &lv_font_montserrat_10))
        return fail();

    empty_ = create_label(view, "", 8, 50, 0x666666, &lv_font_montserrat_12);
    if (!empty_) return fail();

    for (int visible_index = 0; visible_index < SetupWifiListViewModel::VISIBLE_ROWS;
         ++visible_index) {
        auto &row = rows_[visible_index];
        const int y = 30 + visible_index * 22;
        row.background = lv_obj_create(view);
        if (!row.background) return fail();
        lv_obj_set_size(row.background, access.layout().screen_width - 8, 20);
        lv_obj_set_pos(row.background, 4, y);
        lv_obj_set_style_radius(row.background, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row.background, lv_color_hex(0x1F3A5F), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row.background, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row.background, 0, LV_PART_MAIN);
        lv_obj_clear_flag(row.background, LV_OBJ_FLAG_SCROLLABLE);

        row.ssid = create_label(view, "", 8, y + 2, 0xCCCCCC, &lv_font_montserrat_12);
        row.security = create_label(view, "", 180, y + 2, 0xCCCCCC, &lv_font_montserrat_10);
        row.signal = create_label(view, "", 275, y + 2, 0xCCCCCC, &lv_font_montserrat_10);
        if (!row.ssid || !row.security || !row.signal) return fail();
        lv_obj_set_width(row.ssid, 165);
        lv_label_set_long_mode(row.ssid, LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(row.background, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row.ssid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row.security, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row.signal, LV_OBJ_FLAG_HIDDEN);
    }

    hint_ = create_label(view, "", 8, access.content_height() - 14, 0x555555,
                         &lv_font_montserrat_10);
    if (!hint_) return fail();
    commit_view(container, view);
    return true;
}

bool WiFiListView::render(UISetupPage &page, const SetupWifiListSnapshot &snapshot)
{
    if (!mount(page)) return false;
    SetupPageAccess access(page);
    if (title_text_ != snapshot.title) {
        title_text_ = snapshot.title;
        lv_label_set_text(title_, title_text_.c_str());
        access.apply_overflow(title_, 8, access.layout().wifi_title_width, true);
    }
    if (empty_text_ != snapshot.empty_message) {
        empty_text_ = snapshot.empty_message;
        lv_label_set_text(empty_, empty_text_.c_str());
    }
    if (snapshot.rows.empty()) lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    if (hint_text_ != snapshot.hint) {
        hint_text_ = snapshot.hint;
        lv_label_set_text(hint_, hint_text_.c_str());
    }

    for (int index = 0; index < SetupWifiListViewModel::VISIBLE_ROWS; ++index) {
        auto &objects = rows_[index];
        if (index >= static_cast<int>(snapshot.rows.size())) {
            if (objects.visible) {
                lv_obj_add_flag(objects.background, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(objects.ssid, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(objects.security, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(objects.signal, LV_OBJ_FLAG_HIDDEN);
                objects.visible = false;
            }
            continue;
        }
        const auto &row = snapshot.rows[static_cast<std::size_t>(index)];
        const uint32_t color = row.in_use ? 0x58A6FF : (row.selected ? 0xFFFFFF : 0xCCCCCC);
        if (objects.ssid_text != row.ssid) {
            objects.ssid_text = row.ssid;
            lv_label_set_text(objects.ssid, objects.ssid_text.c_str());
        }
        if (objects.security_text != row.security) {
            objects.security_text = row.security;
            lv_label_set_text(objects.security, objects.security_text.c_str());
        }
        if (objects.signal_text != row.signal) {
            objects.signal_text = row.signal;
            lv_label_set_text(objects.signal, objects.signal_text.c_str());
        }
        if (!objects.visible || objects.color != color) {
            objects.color = color;
            lv_obj_set_style_text_color(objects.ssid, lv_color_hex(color), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.security, lv_color_hex(color), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.signal, lv_color_hex(color), LV_PART_MAIN);
        }
        if (!objects.visible || objects.selected != row.selected) {
            objects.selected = row.selected;
            if (row.selected) lv_obj_remove_flag(objects.background, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(objects.background, LV_OBJ_FLAG_HIDDEN);
        }
        if (!objects.visible) {
            lv_obj_remove_flag(objects.ssid, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(objects.security, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(objects.signal, LV_OBJ_FLAG_HIDDEN);
            objects.visible = true;
        }
    }
    return true;
}

bool WiFi::build_list(UISetupPage &page)
{
    return list_view_.render(page, list_view_model_.snapshot());
}

bool WiFi::show_connecting(UISetupPage &page, const char *ssid)
{
    lv_obj_t *container = SetupPageAccess(page).content_container();
    lv_obj_t *view = begin_view(container);
    if (!view) return false;
    char message[128];
    snprintf(message, sizeof(message), "Connecting to %s...", ssid);
    if (!create_label(view, message, 8, 60, 0x58A6FF, &lv_font_montserrat_14)) {
        lv_obj_delete(view);
        return false;
    }
    commit_view(container, view);
    lv_refr_now(nullptr);
    return true;
}

bool WiFi::show_error(UISetupPage &page, const char *message)
{
    lv_obj_t *container = SetupPageAccess(page).content_container();
    lv_obj_t *view = begin_view(container);
    if (!view) return false;
    if (!create_label(view, message, 8, 60, 0xFF4444, &lv_font_montserrat_14)) {
        lv_obj_delete(view);
        return false;
    }
    commit_view(container, view);
    lv_refr_now(nullptr);
    return true;
}

bool WiFi::show_power_warning(UISetupPage &page)
{
    SetupPageAccess access(page);
    lv_obj_t *container = access.content_container();
    lv_obj_t *view = begin_view(container);
    if (!view) return false;

    lv_obj_t *dialog = lv_obj_create(view);
    if (!dialog) {
        lv_obj_delete(view);
        return false;
    }
    lv_obj_set_size(dialog, 280, 92);
    lv_obj_center(dialog);
    lv_obj_set_style_radius(dialog, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0xFFAA00), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(dialog, "WiFi power is off", 12, 10, 0xFFAA00,
                                   &lv_font_montserrat_14);
    lv_obj_t *message = create_label(dialog, "Turn on Power before scanning.", 12, 36,
                                     0xCCCCCC, &lv_font_montserrat_12);
    lv_obj_t *hint = create_label(dialog, "OK", 246, 68, 0x58A6FF,
                                  &lv_font_montserrat_12);
    if (!title || !message || !hint) {
        lv_obj_delete(view);
        return false;
    }
    commit_view(container, view);
    lv_refr_now(nullptr);
    return true;
}

bool WiFi::show_forget_confirmation(UISetupPage &page, const std::string &ssid)
{
    SetupPageAccess access(page);
    lv_obj_t *container = access.content_container();
    lv_obj_t *view = begin_view(container);
    if (!view) return false;
    char message[128];
    snprintf(message, sizeof(message), "Forget '%s'?", ssid.c_str());
    if (!create_label(view, message, 8, 50, 0xFFAA00, &lv_font_montserrat_14) ||
        !create_label(view, "OK:confirm  ESC:cancel", 8,
                      access.content_height() - 14, 0x888888,
                      &lv_font_montserrat_10)) {
        lv_obj_delete(view);
        return false;
    }
    commit_view(container, view);
    lv_refr_now(nullptr);
    return true;
}

WiFiPasswordView::~WiFiPasswordView()
{
    unmount();
}

void WiFiPasswordView::reset_objects()
{
    cp0_keyboard_set_lvgl_keypad_intercept(0);
    root_ = nullptr;
    input_ = nullptr;
    hint_ = nullptr;
    password_visible_ = false;
}

void WiFiPasswordView::root_delete_cb(lv_event_t *event) noexcept
{
    if (!event || lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    auto *view = static_cast<WiFiPasswordView *>(lv_event_get_user_data(event));
    if (view && lv_event_get_target(event) == view->root_) view->reset_objects();
}

void WiFiPasswordView::unmount()
{
    if (root_) lv_obj_delete(root_);
    reset_objects();
}

bool WiFiPasswordView::show(UISetupPage &page, const std::string &ssid)
{
    unmount();
    SetupPageAccess access(page);
    lv_obj_t *container = access.content_container();
    lv_obj_t *view = begin_view(container);
    if (!view) return false;
    root_ = view;
    lv_obj_add_event_cb(view, root_delete_cb, LV_EVENT_DELETE, this);
    auto fail = [&] {
        if (root_) lv_obj_delete(root_);
        reset_objects();
        return false;
    };

    char title_text[128];
    snprintf(title_text, sizeof(title_text), "Connect: %s", ssid.c_str());
    if (!create_label(view, title_text, 10, 10, 0x58A6FF, &lv_font_montserrat_12) ||
        !create_label(view, "Password:", 10, 35, 0xCCCCCC, &lv_font_montserrat_12))
        return fail();

    lv_obj_t *input = lv_textarea_create(view);
    if (!input) return fail();
    lv_obj_remove_style_all(input);
    lv_obj_set_pos(input, 88, 29);
    lv_obj_set_size(input, 210, 30);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, SetupWifiPasswordModel::MAX_PASSWORD_BYTES);
    lv_textarea_set_password_bullet(input, "*");
    lv_textarea_set_password_show_time(input, 0);
    lv_textarea_set_password_mode(input, true);
    lv_textarea_set_text(input, "");
    lv_obj_set_style_text_font(input, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(input, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(input, lv_color_hex(0x181818), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(input, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(input, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(input, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_left(input, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(input, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(input, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(input, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_color(input, lv_color_hex(0x58A6FF), LV_PART_CURSOR);
    lv_obj_set_style_border_width(input, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_side(input, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(input, 400, LV_PART_CURSOR);
    lv_obj_add_state(input, LV_STATE_FOCUSED);

    lv_obj_t *hint = create_label(view, "ALT:show  OK:connect  ESC:cancel", 10,
                                  access.content_height() - 14, 0x555555,
                                  &lv_font_montserrat_10);
    if (!hint) return fail();

    commit_view(container, view);
    input_ = input;
    hint_ = hint;
    password_visible_ = false;
    cp0_keyboard_set_lvgl_keypad_intercept(1);
    access.set_view(SetupViewState::WIFI_PW);
    return true;
}

void WiFiPasswordView::update_password(const std::string &password)
{
    if (!input_) return;
    lv_textarea_set_text(input_, password.c_str());
    lv_textarea_set_cursor_pos(input_, LV_TEXTAREA_CURSOR_LAST);
}

void WiFiPasswordView::toggle_password_visibility()
{
    if (!input_) return;
    password_visible_ = !password_visible_;
    lv_textarea_set_password_mode(input_, !password_visible_);
    set_hint(password_visible_
        ? "ALT:hide  OK:connect  ESC:cancel"
        : "ALT:show  OK:connect  ESC:cancel");
}

void WiFiPasswordView::set_hint(const char *text, uint32_t color)
{
    if (!hint_) return;
    lv_label_set_text(hint_, text ? text : "");
    lv_obj_set_style_text_color(hint_, lv_color_hex(color), LV_PART_MAIN);
}

WiFiSsidView::~WiFiSsidView()
{
    unmount();
}

void WiFiSsidView::reset_objects()
{
    cp0_keyboard_set_lvgl_keypad_intercept(0);
    root_ = nullptr;
    ssid_input_ = nullptr;
    password_input_ = nullptr;
    hint_ = nullptr;
    password_visible_ = false;
}

void WiFiSsidView::root_delete_cb(lv_event_t *event) noexcept
{
    if (!event || lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    auto *view = static_cast<WiFiSsidView *>(lv_event_get_user_data(event));
    if (view && lv_event_get_target(event) == view->root_) view->reset_objects();
}

void WiFiSsidView::unmount()
{
    if (root_) lv_obj_delete(root_);
    reset_objects();
}

bool WiFiSsidView::show(UISetupPage &page)
{
    unmount();
    SetupPageAccess access(page);
    lv_obj_t *container = access.content_container();
    lv_obj_t *view = begin_view(container);
    if (!view) return false;
    root_ = view;
    lv_obj_add_event_cb(view, root_delete_cb, LV_EVENT_DELETE, this);
    auto fail = [&] {
        if (root_) lv_obj_delete(root_);
        reset_objects();
        return false;
    };
    if (!create_label(view, "Add Hidden WiFi", 10, 4, 0x58A6FF,
                      &lv_font_montserrat_12) ||
        !create_label(view, "SSID", 10, 25, 0xCCCCCC, &lv_font_montserrat_10) ||
        !create_label(view, "PASSWORD", 10, 59, 0xCCCCCC, &lv_font_montserrat_10))
        return fail();

    auto create_input = [&](int y, uint32_t max_length) {
        lv_obj_t *input = lv_textarea_create(view);
        if (!input) return static_cast<lv_obj_t *>(nullptr);
        lv_obj_remove_style_all(input);
        lv_obj_set_pos(input, 82, y);
        lv_obj_set_size(input, 216, 28);
        lv_textarea_set_one_line(input, true);
        lv_textarea_set_max_length(input, max_length);
        lv_textarea_set_text(input, "");
        lv_obj_set_style_text_font(input, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(input, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(input, lv_color_hex(0x181818), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(input, lv_color_hex(0x444444), LV_PART_MAIN);
        lv_obj_set_style_border_width(input, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(input, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_left(input, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_top(input, 3, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input, LV_OPA_TRANSP, LV_PART_CURSOR);
        lv_obj_set_style_border_color(input, lv_color_hex(0x58A6FF), LV_PART_CURSOR);
        lv_obj_set_style_border_width(input, 2, LV_PART_CURSOR);
        lv_obj_set_style_border_side(input, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
        lv_obj_set_style_anim_duration(input, 400, LV_PART_CURSOR);
        return input;
    };
    lv_obj_t *ssid_input = create_input(18, SetupWifiSsidModel::MAX_SSID_BYTES);
    lv_obj_t *password_input = create_input(52, SetupWifiPasswordModel::MAX_PASSWORD_BYTES);
    if (!ssid_input || !password_input) return fail();
    lv_textarea_set_password_bullet(password_input, "*");
    lv_textarea_set_password_show_time(password_input, 0);
    lv_textarea_set_password_mode(password_input, true);

    lv_obj_t *hint = create_label(view, "TAB:switch  ALT:show  OK:connect", 10,
                                  access.content_height() - 14, 0x555555,
                                  &lv_font_montserrat_10);
    if (!hint) return fail();
    commit_view(container, view);
    ssid_input_ = ssid_input;
    password_input_ = password_input;
    hint_ = hint;
    password_visible_ = false;
    set_focus(0);
    cp0_keyboard_set_lvgl_keypad_intercept(1);
    access.set_view(SetupViewState::WIFI_SSID);
    return true;
}

void WiFiSsidView::update_ssid(const std::string &ssid)
{
    if (!ssid_input_) return;
    lv_textarea_set_text(ssid_input_, ssid.c_str());
    lv_textarea_set_cursor_pos(ssid_input_, LV_TEXTAREA_CURSOR_LAST);
}

void WiFiSsidView::update_password(const std::string &password)
{
    if (!password_input_) return;
    lv_textarea_set_text(password_input_, password.c_str());
    lv_textarea_set_cursor_pos(password_input_, LV_TEXTAREA_CURSOR_LAST);
}

void WiFiSsidView::set_focus(int focus)
{
    if (!ssid_input_ || !password_input_) return;
    lv_obj_t *focused = focus == 0 ? ssid_input_ : password_input_;
    lv_obj_t *unfocused = focus == 0 ? password_input_ : ssid_input_;
    lv_obj_add_state(focused, LV_STATE_FOCUSED);
    lv_obj_remove_state(unfocused, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(focused, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_set_style_border_color(unfocused, lv_color_hex(0x444444), LV_PART_MAIN);
}

void WiFiSsidView::toggle_password_visibility()
{
    if (!password_input_) return;
    password_visible_ = !password_visible_;
    lv_textarea_set_password_mode(password_input_, !password_visible_);
    set_hint(password_visible_
        ? "TAB:switch  ALT:hide  OK:connect"
        : "TAB:switch  ALT:show  OK:connect");
}

void WiFiSsidView::set_hint(const char *text, uint32_t color)
{
    if (!hint_) return;
    lv_label_set_text(hint_, text ? text : "");
    lv_obj_set_style_text_color(hint_, lv_color_hex(color), LV_PART_MAIN);
}

} // namespace setting
