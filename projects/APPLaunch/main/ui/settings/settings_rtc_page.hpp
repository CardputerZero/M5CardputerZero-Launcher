#pragma once

#include <functional>
#include <memory>
#include <string>

#include "lvgl_components.hpp"

class LvSettingRtcPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRtcPage3();
    LvSettingRtcPage3(lv_obj_t *parent, const NodeIter &parent_node);
    LvSettingRtcPage3(lv_obj_t *parent,
                      const NodeIter &parent_node,
                      std::function<void()> back_callback);
    ~LvSettingRtcPage3() override;

    const std::string &last_error() const noexcept;

protected:
    int initial_selection() const override;

private:
    struct Impl;
    void install_actions();
    void restore_actions() noexcept;
    void create_status_label();
    void start_refresh();
    void set_error(const char *message);
    void clear_error();
    std::unique_ptr<Impl> impl_;
};

class LvSettingRtcConfirmPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRtcConfirmPage3();
    LvSettingRtcConfirmPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback = {});
    ~LvSettingRtcConfirmPage3() override;

    const std::string &last_error() const noexcept;

protected:
    int initial_selection() const override;

private:
    struct Impl;
    void install_actions();
    void restore_actions() noexcept;
    SettingApiResult discard_and_leave();
    SettingApiResult begin_save();
    void create_status_label();
    void set_error(const char *message);
    void clear_error();
    void cancel_backend_request() noexcept;
    void leave_page();
    std::unique_ptr<Impl> impl_;
};

void settings_rtc_ntp_api(int command, void *data) noexcept;

std::unique_ptr<DComponens::LvglComponensBase> settings_rtc_page_factory(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback);

std::unique_ptr<DComponens::LvglComponensBase> settings_rtc_confirm_page_factory(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback);
