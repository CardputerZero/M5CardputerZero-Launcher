#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_ssh.hpp"

#include "input_keys.h"
#include "../keyboard_text_input.hpp"
#include "../model/ssh_shortcut_action.hpp"
#include "sample_log.h"
#include "ui_app_st.hpp"

#include <new>
#include <utility>

UISSHPage::UISSHPage() : AppPage()
{
    set_page_title("SSH");
    load_profile();
    create_ui();
    if (root_screen_ && form_container_) event_handler_init();
}

UISSHPage::~UISSHPage()
{
    restore_operation_.shutdown();
    detach_delete_callbacks();
    if (root_screen_)
        lv_obj_remove_event_cb_with_user_data(
            root_screen_, UISSHPage::static_lvgl_handler, this);
    if (terminal_page_)
        terminal_page_->navigate_home = nullptr;
    terminal_page_.reset();
}

void UISSHPage::do_connect()
{
    const auto validation = model_.validate();
    if (validation.error != SshConnectionModel::Error::NONE) {
        set_status(validation.message, true);
        return;
    }
    if (!persist_profile())
        SLOGW("[SSH] Unable to persist profile before connecting");
    const auto arguments = model_.arguments();
    SLOGI("[SSH] Launching structured ssh request");

    restore_operation_.abort(restore_token_);
    restore_token_ = restore_operation_.begin();
    if (!restore_token_)
        return;

    try {
        terminal_page_ = std::make_shared<UISTPage>();
    } catch (...) {
        restore_operation_.abort(restore_token_);
        set_status("Unable to create SSH terminal", true);
        return;
    }
    if (!terminal_page_ || !terminal_page_->screen()) {
        terminal_page_.reset();
        restore_operation_.abort(restore_token_);
        set_status("Unable to create SSH screen", true);
        return;
    }
    terminal_page_->navigate_home = [page = this, token = restore_token_]() {
        if (!token.current()) return;
        UISSHPage *self = page;
        if (self->terminal_return_pending_) return;
        self->terminal_return_pending_ = true;
        struct RestoreContext {
            UISSHPage *page;
            setting::AsyncOperationLifecycle::Token token;
        };
        auto *context = new (std::nothrow) RestoreContext{self, token};
        if (!context) {
            self->terminal_return_pending_ = false;
            return;
        }
        if (lv_async_call([](void *user) noexcept {
            std::unique_ptr<RestoreContext> context(static_cast<RestoreContext *>(user));
            bool owner_confirmed = false;
            try {
                owner_confirmed = ssh_restore_completion_allowed(
                    context->token.complete(), context->page);
                if (!owner_confirmed) return;
                context->page->restore_input_view();
            } catch (...) {
                if (owner_confirmed)
                    context->page->terminal_return_pending_ = false;
            }
        }, context) != LV_RESULT_OK) {
            delete context;
            self->terminal_return_pending_ = false;
        }
    };

    try {
        terminal_page_->exec("ssh", arguments);
    } catch (...) {
        terminal_page_->navigate_home = nullptr;
        terminal_page_.reset();
        restore_operation_.abort(restore_token_);
        set_status("Unable to start SSH", true);
        return;
    }

    view_state_ = ViewState::TERMINAL;
    terminal_return_pending_ = false;
    lv_disp_load_scr(terminal_page_->screen());
    if (lv_indev_t *input = lv_indev_get_next(nullptr)) {
        lv_indev_set_group(input, terminal_page_->input_group());
    }
}

void UISSHPage::set_status(std::string message, bool error)
{
    SshConnectionModel previous = model_;
    status_message_ = std::move(message);
    status_error_ = error;
    rebuild_or_restore(std::move(previous));
}

void UISSHPage::load_profile()
{
    std::string host = "192.168.1.1", port = "22", user = "pi";
    auto read = [](const char *key, std::string &value) {
        cp0_signal_config_api({"GetStr", key, value},
            [&](int code, std::string data) { if (code == 0) value = std::move(data); });
    };
    read("ssh_host", host);
    read("ssh_port", port);
    read("ssh_user", user);
    if (!model_.set_values(std::move(host), std::move(port), std::move(user)))
        status_message_ = "Saved SSH profile is invalid";
}

bool UISSHPage::persist_profile()
{
    bool ok = false;
    cp0_signal_config_api(
        {"SetManyAndSave",
         "ssh_host", model_.value(0),
         "ssh_port", model_.value(1),
         "ssh_user", model_.value(2)},
        [&](int code, std::string) { ok = code == 0; });
    return ok;
}

void UISSHPage::save_profile()
{
    const auto validation = model_.validate();
    if (validation.error != SshConnectionModel::Error::NONE) {
        set_status(validation.message, true);
        return;
    }
    const bool ok = persist_profile();
    set_status(ok ? "Profile saved" : "Unable to save profile", !ok);
}

void UISSHPage::restore_input_view()
{
    if (screen()) lv_disp_load_scr(screen());
    if (lv_indev_t *input = lv_indev_get_next(nullptr)) {
        lv_indev_set_group(input, input_group());
    }
    if (terminal_page_)
        terminal_page_->navigate_home = nullptr;
    terminal_page_.reset();
    view_state_ = ViewState::INPUT;
    terminal_return_pending_ = false;
}

void UISSHPage::event_handler_init()
{
    if (!root_screen_) return;
    lv_obj_add_event_cb(root_screen_, UISSHPage::static_lvgl_handler, LV_EVENT_ALL, this);
}

void UISSHPage::static_lvgl_handler(lv_event_t *event) noexcept
{
    try {
        if (!event) return;
        auto *page = static_cast<UISSHPage *>(lv_event_get_user_data(event));
        if (!page || !ssh_page_event_callback_allowed(
                lv_event_get_current_target(event), page->root_screen_))
            return;
        page->event_handler(event);
    } catch (...) {
    }
}

void UISSHPage::rebuild_or_restore(SshConnectionModel previous) noexcept
{
    try {
        if (build_input_fields()) return;
    } catch (...) {
    }
    model_ = std::move(previous);
}

void UISSHPage::event_handler(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_DELETE &&
        lv_event_get_target(event) == lv_event_get_current_target(event)) {
        restore_operation_.shutdown();
        if (terminal_page_)
            terminal_page_->navigate_home = nullptr;
        return;
    }
    if (lv_event_get_code(event) != static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD)) return;
    const struct key_item *item = launcher_ui::events::keyboard_item(event);
    if (!launcher_ui::events::is_key_released(event)) return;

    if (view_state_ == ViewState::HELP) {
        if (launcher_ui::events::keyboard_key(event) == KEY_ESC) close_help();
        return;
    }
    if (view_state_ != ViewState::INPUT) return;

    switch (ssh_shortcut_action(item)) {
    case SshShortcutAction::PREVIOUS_FIELD: {
        SshConnectionModel previous = model_;
        if (model_.select_previous()) rebuild_or_restore(std::move(previous));
        return;
    }
    case SshShortcutAction::NEXT_FIELD: {
        SshConnectionModel previous = model_;
        if (model_.select_next()) rebuild_or_restore(std::move(previous));
        return;
    }
    case SshShortcutAction::SHOW_HELP:
        show_help();
        return;
    case SshShortcutAction::NONE:
        break;
    }

    const uint32_t key = launcher_ui::events::keyboard_key(event);
    switch (key) {
    case KEY_ENTER:
        do_connect();
        break;
    case KEY_TAB:
        save_profile();
        break;
    case KEY_ESC:
        if (navigate_home) navigate_home();
        break;
    case KEY_BACKSPACE: {
        SshConnectionModel previous = model_;
        if (model_.erase_last()) rebuild_or_restore(std::move(previous));
        break;
    }
    default: {
        SshConnectionModel previous = model_;
        if (model_.append(launcher_ui::text_input::key_text(key, item)))
            rebuild_or_restore(std::move(previous));
        break;
    }
    }
}

void UISSHPage::static_owned_obj_delete_cb(lv_event_t *event) noexcept
{
    try {
        if (!event || !ssh_owned_delete_callback_allowed(
                lv_event_get_target(event), lv_event_get_current_target(event)))
            return;
        auto *self = static_cast<UISSHPage *>(lv_event_get_user_data(event));
        if (!self) return;
        lv_obj_t *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (deleted == self->background_) {
            self->background_ = nullptr;
            self->form_container_ = nullptr;
            self->help_container_ = nullptr;
        } else if (deleted == self->form_container_) {
            self->form_container_ = nullptr;
        } else if (deleted == self->help_container_) {
            self->help_container_ = nullptr;
        }
    } catch (...) {
    }
}

void UISSHPage::detach_delete_callbacks()
{
    if (help_container_)
        lv_obj_remove_event_cb_with_user_data(
            help_container_, static_owned_obj_delete_cb, this);
    if (form_container_)
        lv_obj_remove_event_cb_with_user_data(
            form_container_, static_owned_obj_delete_cb, this);
    if (background_)
        lv_obj_remove_event_cb_with_user_data(
            background_, static_owned_obj_delete_cb, this);
    form_container_ = nullptr;
    help_container_ = nullptr;
    background_ = nullptr;
}
