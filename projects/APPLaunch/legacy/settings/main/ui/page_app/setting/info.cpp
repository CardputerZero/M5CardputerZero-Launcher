#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"
#include "../../model/setup_info_timer_contract.hpp"

#include <algorithm>

namespace setting {

Info::~Info()
{
    stop_timer();
    reset_visible_labels();
}

void Info::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    UISetupPage *page = &p;
    Info *controller = this;
    MenuItem m;
    m.label = "Info";
    m.sub_items = {
        {"Battery: --%", false, false, nullptr},
        {"Temp: --C", false, false, nullptr},
        {"Current: --mA", false, false, nullptr},
        {"Voltage: --V", false, false, nullptr},
        {"BQ Calibrate", false, false, [controller, page]() { controller->enter_bq_calibrate(*page); }},
    };
    m.on_enter = [controller, page]() { controller->refresh_values(*page); controller->start_timer(*page); };
    menu.push_back(m);
}

void Info::refresh_values(UISetupPage &page)
{
    MenuItem *menu = SetupPageAccess(page).find_menu("Info");
    if (menu) {
        int result_code = -1;
        std::string response;
        cp0_signal_bq27220_api({"Read"}, [&](int code, std::string data) {
            result_code = code;
            response = std::move(data);
        });
        model_.update(result_code, response);
        const auto &labels = model_.labels();
        const std::size_t count = std::min(labels.size(), menu->sub_items.size());
        for (std::size_t index = 0; index < count; ++index)
            menu->sub_items[index].label = labels[index];
    }
    if (SetupPageAccess(page).is_view(SetupViewState::SUB)) refresh_visible_labels(page);
}

void Info::reset_visible_labels()
{
    for (int i = 0; i < 4; ++i) {
        if (sub_labels_[i])
            lv_obj_remove_event_cb_with_user_data(
                sub_labels_[i], visible_label_delete_cb, this);
        sub_labels_[i] = nullptr;
        sub_label_focused_[i] = false;
        visible_text_[i].clear();
    }
}

void Info::track_visible_label(int index, lv_obj_t *label, bool focused, const std::string &text)
{
    if (index < 0 || index >= 4)
        return;

    if (sub_labels_[index])
        lv_obj_remove_event_cb_with_user_data(
            sub_labels_[index], visible_label_delete_cb, this);
    sub_labels_[index] = label;
    if (label)
        lv_obj_add_event_cb(label, visible_label_delete_cb, LV_EVENT_DELETE, this);
    sub_label_focused_[index] = focused;
    visible_text_[index] = text;
}

void Info::refresh_visible_labels(UISetupPage &page)
{
    SetupPageAccess access(page);
    MenuItem *item = access.selected_menu();
    if (!item || item->label != "Info")
        return;

    for (int i = 0; i < 4 && i < static_cast<int>(item->sub_items.size()); ++i) {
        lv_obj_t *lbl = sub_labels_[i];
        if (!lbl)
            continue;

        const char *new_text = item->sub_items[i].label.c_str();
        if (visible_text_[i] == new_text)
            continue;

        lv_label_set_text(lbl, new_text);
        visible_text_[i] = new_text;
        const auto layout = access.layout();
        access.apply_overflow_centered(lbl, layout.sub_center_x,
            sub_label_focused_[i] ? 80 : layout.value_box_width, sub_label_focused_[i]);
    }
}

void Info::start_timer(UISetupPage &page)
{
    stop_timer();
    timer_page_ = &page;
    timer_.start(
        [this] {
            return lv_timer_create(timer_cb, 1000, this);
        }, [](lv_timer_t *timer) { lv_timer_delete(timer); });
    if (!timer_.active()) timer_page_ = nullptr;
}

void Info::timer_cb(lv_timer_t *timer) noexcept
{
    auto *info = static_cast<Info *>(lv_timer_get_user_data(timer));
    if (!info || !setup_info_timer_callback_ready(
            timer, info->timer_.current(timer), info->timer_page_)) return;
    try {
        SetupPageAccess access(*info->timer_page_);
        if (access.is_view(SetupViewState::SUB))
            access.info().refresh_values(*info->timer_page_);
    } catch (...) {
        info->stop_timer();
    }
}

void Info::stop_timer()
{
    timer_.stop();
    timer_page_ = nullptr;
}

void Info::enter_bq_calibrate(UISetupPage &page)
{
    SetupPageAccess(page).enter_value(
        "BQ Calib", {"Enter CAL", "CC Offset", "Board Offset", "Exit CAL"}, 0);
}

void Info::apply_bq_calibrate(UISetupPage &page)
{
    const int selection = SetupPageAccess(page).value_selection();
    if (selection < 0 || selection > 3) return;
    cp0_signal_bq27220_api(
        {"Calibrate", std::to_string(selection)}, nullptr);
}

void Info::visible_label_delete_cb(lv_event_t *event) noexcept
{
    if (!event || !setup_info_label_delete_callback_allowed(
            lv_event_get_target(event), lv_event_get_current_target(event))) return;
    auto *self = static_cast<Info *>(lv_event_get_user_data(event));
    if (!self) return;
    lv_obj_t *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
    for (int index = 0; index < 4; ++index) {
        if (self->sub_labels_[index] != deleted) continue;
        self->sub_labels_[index] = nullptr;
        self->sub_label_focused_[index] = false;
        self->visible_text_[index].clear();
    }
}

} // namespace setting
