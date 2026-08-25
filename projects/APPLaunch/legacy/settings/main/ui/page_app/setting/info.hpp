#pragma once

#include "menu_types.hpp"
#include "../../model/setup_info_model.hpp"
#include "../../model/page_timer_lifecycle.hpp"

#include <lvgl.h>

#include <string>
#include <vector>

class UISetupPage;

namespace setting {

class Info
{
public:
    ~Info();
    void append(UISetupPage &page, std::vector<MenuItem> &menu);
    void refresh_values(UISetupPage &page);
    void start_timer(UISetupPage &page);
    void stop_timer();
    void enter_bq_calibrate(UISetupPage &page);
    void apply_bq_calibrate(UISetupPage &page);
    void reset_visible_labels();
    void track_visible_label(int index, lv_obj_t *label, bool focused, const std::string &text);
    void refresh_visible_labels(UISetupPage &page);

private:
    static void timer_cb(lv_timer_t *timer) noexcept;
    static void visible_label_delete_cb(lv_event_t *event) noexcept;

    PageTimerLifecycle<lv_timer_t *> timer_;
    UISetupPage *timer_page_ = nullptr;
    lv_obj_t *sub_labels_[4] = {};
    bool sub_label_focused_[4] = {};
    std::string visible_text_[4];
    SetupInfoModel model_;
};

} // namespace setting
