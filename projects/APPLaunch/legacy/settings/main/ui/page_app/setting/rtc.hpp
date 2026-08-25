#pragma once

#include "menu_types.hpp"
#include "../../model/rtc_state_model.hpp"
#include "../../model/async_operation_lifecycle.hpp"

#include "cp0_lvgl_app.h"
#include <lvgl.h>

#include <cstdint>
#include <vector>

class UISetupPage;

namespace setting {

class RTC
{
public:
    RTC();
    ~RTC();
    void shutdown();

    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void refresh_values(UISetupPage &page);
    void toggle_ntp(UISetupPage &page);
    void enter_adjust(UISetupPage &page, int field);
    void apply_value(UISetupPage &page);
    void commit_to_hardware(UISetupPage &page);
    void show_write_confirm(UISetupPage &page);
    void close_write_confirm();
    void handle_write_confirm_key(UISetupPage &page, uint32_t key);

    bool is_dirty() const { return state_.dirty(); }
    bool ntp_on() const { return state_.ntp_on(); }
    bool ntp_available() const { return state_.ntp_available(); }
    bool write_confirm_active() const { return confirm_overlay_ != nullptr; }
    void clear_dirty() { state_.discard_edits(); }

private:
    enum class Modal
    {
        NONE,
        CONFIRM,
        BUSY,
        ERROR
    };

    void show_status(const char *title, const char *detail, Modal modal);
    void show_result_error(cp0_sudo_result_t result, int exit_code, const char *operation);
    void update_labels(UISetupPage &page);
    void update_write_confirm_buttons();
    static void overlay_delete_cb(lv_event_t *event) noexcept;

    RtcStateModel state_;
    RtcField field_ = RtcField::YEAR;
    bool ntp_previous_ = true;
    Modal modal_ = Modal::NONE;
    AsyncOperationLifecycle async_operation_;
    RtcWriteConfirmModel confirm_model_;
    RtcOverlayLifecycleModel overlay_lifecycle_;
    RtcOverlayLifecycleModel::Token overlay_token_ = 0;
    lv_obj_t *confirm_overlay_ = nullptr;
    lv_obj_t *confirm_yes_lbl_ = nullptr;
    lv_obj_t *confirm_no_lbl_ = nullptr;
};

} // namespace setting
