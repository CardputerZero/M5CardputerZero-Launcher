#pragma once

#include "../../model/adb_state.hpp"
#include "menu_types.hpp"
#include "../../model/developer_page_model.hpp"
#include "../../model/async_operation_lifecycle.hpp"

#include "cp0_lvgl_app.h"
#include <lvgl.h>

#include <cstdint>
#include <list>
#include <string>
#include <vector>

class UISetupPage;

namespace setting {

class Developer
{
public:
    Developer();
    ~Developer();
    void shutdown();

    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void toggle_adb(UISetupPage &page);
    bool refresh_adb_status(UISetupPage &page);
    void enter_usb_guide(UISetupPage &page, bool enabling);
    void build_usb_guide_view(UISetupPage &page);
    void stop_usb_guide_anims();
    void handle_usb_guide_key(UISetupPage &page, uint32_t key);
    void handle_status_key(UISetupPage &page, uint32_t key);
    void enter_pair_view(UISetupPage &page, bool enable_after_pair = false);
    void enter_revoke_view(UISetupPage &page);
    void build_authorizations_view(UISetupPage &page);
    void handle_authorizations_key(UISetupPage &page, uint32_t key);
    void handle_pair_key(UISetupPage &page, uint32_t key);
    void clear_authorizations(UISetupPage &page);
    bool status_active() const { return status_overlay_ != nullptr; }

private:
    enum class Modal
    {
        NONE,
        BUSY,
        ERROR
    };

    static lv_obj_t *guide_chip(lv_obj_t *parent,
                                int x,
                                int y,
                                int width,
                                int height,
                                uint32_t background,
                                uint32_t border,
                                int radius,
                                int border_width);
    static lv_obj_t *guide_label(lv_obj_t *parent,
                                 int x,
                                 int y,
                                 const char *text,
                                 uint32_t color,
                                 const lv_font_t *font);
    void show_status(const char *title, const char *detail, Modal modal);
    void close_status();
    static void status_overlay_delete_cb(lv_event_t *event) noexcept;
    static void view_object_delete_cb(lv_event_t *event) noexcept;
    void update_toggle(UISetupPage &page, bool enabled);
    void show_result_error(cp0_sudo_result_t result, int exit_code);
    void submit_pairing(UISetupPage &page);
    void run_admin_action(UISetupPage &page,
                          std::list<std::string> args,
                          const char *title,
                          const char *detail,
                          bool enable_after);

    AsyncOperationLifecycle async_operation_;
    DeveloperOverlayLifecycleModel overlay_lifecycle_;
    Modal modal_ = Modal::NONE;
    lv_obj_t *status_overlay_ = nullptr;
    bool usb_guide_enabling_ = true;
    lv_obj_t *usb_guide_knob_ = nullptr;
    DeveloperPageModel model_;
    lv_obj_t *pair_input_label_ = nullptr;
    lv_obj_t *pair_hint_label_ = nullptr;
    std::vector<AdbAuthorization> authorizations_;
};

} // namespace setting
