#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

#include <utility>

namespace setting {

std::vector<MenuItem> &SetupPageAccess::menus()
{
    return page_.menu_items_;
}

MenuItem *SetupPageAccess::find_menu(std::string_view label)
{
    return find_menu_item(page_.menu_items_, label);
}

MenuItem *SetupPageAccess::selected_menu()
{
    return selected_menu_item(page_.menu_items_, page_.model_.selected_index);
}

Screen &SetupPageAccess::screen()
{
    return page_.screen_;
}

Speaker &SetupPageAccess::speaker()
{
    return page_.speaker_;
}

Camera &SetupPageAccess::camera()
{
    return page_.camera_;
}

WiFi &SetupPageAccess::wifi() { return page_.wifi_; }
Info &SetupPageAccess::info() { return page_.info_; }
Developer &SetupPageAccess::developer() { return page_.developer_; }
RTC &SetupPageAccess::rtc() { return page_.rtc_; }
BluetoothUiSession &SetupPageAccess::bluetooth() { return page_.ensure_bluetooth_ui(); }
void SetupPageAccess::release_bluetooth_ui() { page_.release_bluetooth_ui(); }
SoundCard &SetupPageAccess::soundcard() { return page_.soundcard_; }

void SetupPageAccess::confirm(const char *title, std::function<void()> action)
{
    page_.enter_confirm_action(title, std::move(action));
}

void SetupPageAccess::enter_value(
    std::string title, std::vector<std::string> options, int selected_index)
{
    page_.model_.enter_value(std::move(title), std::move(options), selected_index);
    page_.transition_enter_level();
}

const std::string &SetupPageAccess::value_title() const
{
    return page_.model_.value_title;
}

int SetupPageAccess::value_selection() const
{
    return page_.model_.value_selected_index;
}

int SetupPageAccess::config_get_int(const char *key, int default_value) const
{
    return UISetupPage::config_get_int(key, default_value);
}

bool SetupPageAccess::config_set_int(const char *key, int value) const
{
    return UISetupPage::config_set_int(key, value);
}

bool SetupPageAccess::config_save() const
{
    return UISetupPage::config_save();
}

int SetupPageAccess::audio_volume_read() const
{
    return UISetupPage::audio_volume_read();
}

int SetupPageAccess::audio_volume_write(int value) const
{
    return UISetupPage::audio_volume_write(value);
}

bool SetupPageAccess::gpio_set(const char *name, int value) const
{
    return UISetupPage::gpio_set(name, value);
}

int SetupPageAccess::gpio_get(const char *name) const
{
    return UISetupPage::gpio_get(name);
}

void SetupPageAccess::set_view(SetupViewState view)
{
    page_.model_.view = view;
    page_.input_context_scope_.update(setup_input_context(view));
}

void SetupPageAccess::select_sub(int index, int item_count)
{
    page_.model_.select_sub(index, item_count);
}

bool SetupPageAccess::enter_help() { return page_.model_.enter_help(); }
bool SetupPageAccess::leave_help() { return page_.model_.leave_help(); }

SetupViewState SetupPageAccess::view() const { return page_.model_.view; }
bool SetupPageAccess::is_view(SetupViewState view) const { return page_.model_.view == view; }

lv_obj_t *SetupPageAccess::content_container()
{
    const auto found = page_.ui_obj_.find("list_cont");
    return found == page_.ui_obj_.end() ? nullptr : found->second;
}

int SetupPageAccess::content_height() const
{
    return UISetupPage::LIST_H;
}

SetupLayout SetupPageAccess::layout() const
{
    return fixed_layout();
}

SetupLayout SetupPageAccess::fixed_layout()
{
    return {UISetupPage::SCREEN_W, UISetupPage::SCREEN_H, UISetupPage::LIST_H,
        UISetupPage::WIFI_TITLE_BOX_W, UISetupPage::SUB_CENTER_X, UISetupPage::VALUE_BOX_W,
        UISetupPage::SC_MARGIN_X, UISetupPage::SC_ROW_X, UISetupPage::SC_CARD_NAME_W,
        UISetupPage::SC_CTRL_NAME_X, UISetupPage::SC_CTRL_NAME_W, UISetupPage::SC_CTRL_VALUE_X,
        UISetupPage::SC_CTRL_VALUE_W, UISetupPage::SC_DETAIL_TEXT_W, UISetupPage::SC_INPUT_X,
        UISetupPage::SC_INPUT_W, UISetupPage::SC_BOTTOM_HINT_W};
}

std::string_view SetupPageAccess::current_utf8() const
{
    return page_.cur_elm_ && page_.cur_elm_->utf8[0] ? std::string_view(page_.cur_elm_->utf8) : std::string_view{};
}

const std::string &SetupPageAccess::arrow_up_path() const { return page_.img_arrow_up_; }
const std::string &SetupPageAccess::arrow_down_path() const { return page_.img_arrow_down_; }
void SetupPageAccess::play_enter() { page_.play_enter(); }
void SetupPageAccess::play_back() { page_.play_back(); }
void SetupPageAccess::build_main_view() { page_.build_main_view(); }
void SetupPageAccess::build_sub_view() { page_.build_sub_view(); }
void SetupPageAccess::rebuild_view() { page_.rebuild_view(); }
void SetupPageAccess::transition_enter() { page_.transition_enter_level(); }
void SetupPageAccess::transition_back() { page_.transition_back_level(); }
void SetupPageAccess::apply_overflow(lv_obj_t *label, int left, int width, bool focused) const
{
    UISetupPage::apply_overflow_handling(label, left, width, focused);
}
void SetupPageAccess::apply_overflow_centered(
    lv_obj_t *label, int center, int width, bool focused) const
{
    UISetupPage::apply_overflow_centered(label, center, width, focused);
}
void SetupPageAccess::apply_fixed_label_box(
    lv_obj_t *label, int x, int y, int width, bool scroll) const
{
    UISetupPage::apply_fixed_label_box(label, x, y, width, scroll);
}

} // namespace setting
