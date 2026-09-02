#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_ip_panel.hpp"

#include "cp0_lvgl_app.h"
#include "input_keys.h"
#include "../model/ip_panel_shortcut_action.hpp"

#include <utility>

UIIpPanelPage::UIIpPanelPage() : AppPage()
{
    try {
        set_page_title("IP INFO");
        create_ui();
        if (root_screen_ && ip_panel_view_ready(background_, list_container_))
            event_handler_init();
    } catch (...) {
        refresh_enabled_ = false;
        try {
            refresh_timer_.stop();
        } catch (...) {
        }
        lv_obj_t *background = background_;
        detach_delete_callbacks();
        if (background) lv_obj_delete(background);
    }
}

UIIpPanelPage::~UIIpPanelPage()
{
    refresh_timer_.stop();
    detach_delete_callbacks();
    if (root_screen_)
        lv_obj_remove_event_cb_with_user_data(
            root_screen_, UIIpPanelPage::static_lvgl_handler, this);
}

bool UIIpPanelPage::fetch_iface_list()
{
    cp0_netif_info_t entries[32]{};
    int count = 0;
    if (cp0_network_list(entries, 32, &count) != 0) {
        return false;
    }
    if (count < 0) count = 0;
    if (count > 32) count = 32;

    std::vector<IpInterfaceInfo> interfaces;
    interfaces.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        interfaces.push_back({entries[index].iface, entries[index].ipv4,
                              entries[index].netmask, entries[index].is_up != 0});
    }
    return model_.apply_refresh(true, std::move(interfaces));
}

void UIIpPanelPage::static_timer_cb(lv_timer_t *timer) noexcept
{
    auto *page = static_cast<UIIpPanelPage *>(lv_timer_get_user_data(timer));
    if (!page) return;
    try {
        if (ip_panel_refresh_callback_allowed(
                page->refresh_timer_.current(timer), page->refresh_enabled_))
            page->on_timer_refresh();
    } catch (...) {
        page->refresh_enabled_ = false;
    }
}

void UIIpPanelPage::on_timer_refresh()
{
    if (fetch_iface_list()) build_list_rows();
}

void UIIpPanelPage::event_handler_init()
{
    if (!root_screen_) return;
    lv_obj_add_event_cb(root_screen_, UIIpPanelPage::static_lvgl_handler, LV_EVENT_ALL, this);
}

void UIIpPanelPage::static_lvgl_handler(lv_event_t *event) noexcept
{
    if (!event) return;
    auto *page = static_cast<UIIpPanelPage *>(lv_event_get_user_data(event));
    if (!page || !ip_panel_root_callback_allowed(
            lv_event_get_current_target(event), page->root_screen_)) return;
    try {
        page->event_handler(event);
    } catch (...) {
        page->refresh_enabled_ = false;
        page->refresh_timer_.stop();
    }
}

uint32_t UIIpPanelPage::fzxc_to_lv_arrow(uint32_t key)
{
    switch (key) {
    case KEY_F: return LV_KEY_UP;
    case KEY_X: return LV_KEY_DOWN;
    case KEY_Z: return LV_KEY_LEFT;
    case KEY_C: return LV_KEY_RIGHT;
    default: return key;
    }
}

void UIIpPanelPage::event_handler(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_DELETE &&
        lv_event_get_target(event) == lv_event_get_current_target(event)) {
        refresh_timer_.stop();
        list_container_ = nullptr;
        help_container_ = nullptr;
        return;
    }

    const struct key_item *item = launcher_ui::events::keyboard_item(event);
    if (view_state_ == ViewState::MAIN && launcher_ui::events::is_key_pressed(event) &&
        ip_panel_shortcut_action(item) == IpPanelShortcutAction::SHOW_HELP) {
        show_help();
        return;
    }

    if (lv_event_get_code(event) == static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD)) {
        // The matching LV_EVENT_KEY release below owns navigation and closes
        // the help view without also returning home from the page.
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_KEY) return;

    if (view_state_ == ViewState::HELP) {
        if (fzxc_to_lv_arrow(lv_event_get_key(event)) == LV_KEY_ESC)
            close_help();
        return;
    }

    switch (fzxc_to_lv_arrow(lv_event_get_key(event))) {
    case LV_KEY_UP:
        if (model_.select_previous()) build_list_rows();
        break;
    case LV_KEY_DOWN:
        if (model_.select_next()) build_list_rows();
        break;
    case LV_KEY_ESC:
        refresh_timer_.stop();
        if (navigate_home) navigate_home();
        break;
    default:
        break;
    }
}

void UIIpPanelPage::static_owned_obj_delete_cb(lv_event_t *event) noexcept
{
    UIIpPanelPage *self = nullptr;
    try {
        if (!event || !ip_panel_owned_delete_callback_allowed(
                lv_event_get_target(event), lv_event_get_current_target(event)))
            return;
        self = static_cast<UIIpPanelPage *>(lv_event_get_user_data(event));
        if (!self) return;
        lv_obj_t *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (deleted == self->background_) {
            self->background_ = nullptr;
            self->list_container_ = nullptr;
            self->help_container_ = nullptr;
            self->refresh_enabled_ = false;
            self->refresh_timer_.stop();
        } else if (deleted == self->list_container_) {
            self->list_container_ = nullptr;
            self->refresh_enabled_ = false;
            self->refresh_timer_.stop();
        } else if (deleted == self->help_container_) {
            self->help_container_ = nullptr;
        }
    } catch (...) {
        if (self) self->refresh_enabled_ = false;
    }
}

void UIIpPanelPage::detach_delete_callbacks()
{
    if (help_container_)
        lv_obj_remove_event_cb_with_user_data(
            help_container_, static_owned_obj_delete_cb, this);
    if (list_container_)
        lv_obj_remove_event_cb_with_user_data(
            list_container_, static_owned_obj_delete_cb, this);
    if (background_)
        lv_obj_remove_event_cb_with_user_data(
            background_, static_owned_obj_delete_cb, this);
    list_container_ = nullptr;
    help_container_ = nullptr;
    background_ = nullptr;
}
