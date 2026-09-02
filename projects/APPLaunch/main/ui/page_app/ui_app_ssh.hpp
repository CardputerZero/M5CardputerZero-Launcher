/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include "../launcher_ui_app_page.hpp"
#include "../model/async_operation_lifecycle.hpp"
#include "../model/ssh_connection_model.hpp"
#include "../model/ssh_view_build_contract.hpp"
#include <memory>
#include <string>

class UISTPage;

// ============================================================
//  SSH Client  UISSHPage
//  Screen: 320 x 170  (top bar 20px, ui_APP_Container 320x150)
//
//  Views:
//    VIEW_INPUT    -- Host/Port/User input fields
//    VIEW_HELP     -- SSH controls and shortcut reference
//    VIEW_TERMINAL -- Embedded UISTPage running ssh
// ============================================================

class UISSHPage : public AppPage
{
    enum class ViewState { INPUT, HELP, TERMINAL };

public:
    UISSHPage();

    ~UISSHPage();

private:
    // ==================== data members ====================
    SshConnectionModel model_;
    lv_obj_t *background_ = nullptr;
    lv_obj_t *form_container_ = nullptr;
    lv_obj_t *help_container_ = nullptr;
    ViewState view_state_ = ViewState::INPUT;
    std::shared_ptr<UISTPage> terminal_page_;
    bool terminal_return_pending_ = false;
    setting::AsyncOperationLifecycle restore_operation_;
    setting::AsyncOperationLifecycle::Token restore_token_;
    std::string status_message_;
    bool status_error_ = false;

    // ==================== UI construction (input view) ====================
    void create_ui();

    bool build_input_fields();
    bool build_help_view();
    void rebuild_or_restore(SshConnectionModel previous) noexcept;
    void show_help();
    void close_help();

    // ==================== connect via SSH ====================
    void do_connect();
    bool persist_profile();
    void save_profile();
    void load_profile();
    void set_status(std::string message, bool error);

    void restore_input_view();

    // ==================== event binding ====================
    void event_handler_init();

    static void static_lvgl_handler(lv_event_t *e) noexcept;
    static void static_owned_obj_delete_cb(lv_event_t *event) noexcept;

    void event_handler(lv_event_t *e);
    void detach_delete_callbacks();
};
