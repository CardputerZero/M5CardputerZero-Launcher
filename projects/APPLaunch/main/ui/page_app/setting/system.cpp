#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "../../launcher_toast.h"
#include "../../model/system_page_model.hpp"
#include "setup_page_access.hpp"

namespace setting {

namespace {

void apply_extport_toggle(UISetupPage &page,
                          std::size_t index,
                          const char *gpio_name)
{
    SetupPageAccess access(page);
    MenuItem *menu = access.find_menu("ExtPort");
    if (!menu || index >= menu->sub_items.size()) return;

    SubItem &item = menu->sub_items[index];
    const bool desired = item.toggle_state;
    const int current = access.gpio_get(gpio_name);
    const bool previous = current >= 0 ? current != 0 : !desired;
    const int previous_config = access.config_get_int(gpio_name, previous ? 1 : 0);
    const bool gpio_succeeded = access.gpio_set(gpio_name, desired ? 1 : 0);
    const bool config_succeeded = gpio_succeeded &&
        access.config_set_int(gpio_name, desired ? 1 : 0) && access.config_save();
    if (gpio_succeeded && !config_succeeded) {
        access.config_set_int(gpio_name, previous_config != 0 ? 1 : 0);
        access.gpio_set(gpio_name, previous ? 1 : 0);
    }
    item.toggle_state = system_page::extport_toggle_value(
        previous, desired, gpio_succeeded && config_succeeded);
}

} // namespace

void About::append(UISetupPage &page, std::vector<MenuItem> &menu)
{
    UISetupPage *page_ptr = &page;
    MenuItem item;
    item.label = "About";
    item.sub_items = {
        {"CardputerZero", false, false, nullptr},
        {"LVGL 9.x", false, false, nullptr},
        {"", false, false, nullptr},
        {"", false, false, nullptr},
    };
    item.on_enter = [page_ptr]() { About::refresh_info(*page_ptr); };
    menu.push_back(item);
}

void About::refresh_info(UISetupPage &page)
{
    MenuItem *item = SetupPageAccess(page).find_menu("About");
    if (!item || item->sub_items.size() < 4)
        return;
    item->sub_items[0].label = "M5CardputerZero";
    item->sub_items[1].label = "LVGL: 9.x";
    char text[64];
    snprintf(text, sizeof(text), "Build: %s", __DATE__);
    item->sub_items[2].label = text;
    snprintf(text, sizeof(text), "Commit: %s", LAUNCHER_GIT_COMMIT);
    item->sub_items[3].label = text;
}

void Help::append(UISetupPage &page, std::vector<MenuItem> &menu)
{
    UISetupPage *page_ptr = &page;
    MenuItem item;
    item.label = "Help";
    item.sub_items = {{"View Help", false, false, [page_ptr]() { Help::enter_page(*page_ptr); }}};
    menu.push_back(item);
}

void Help::enter_page(UISetupPage &page)
{
    SetupPageAccess access(page);
    if (build_page(page)) access.enter_help();
}

bool Help::build_page(UISetupPage &page)
{
    SetupPageAccess access(page);
    lv_obj_t *container = access.content_container();
    if (!container)
        return false;
    lv_obj_t *candidate = lv_obj_create(container);
    if (!candidate) return false;
    lv_obj_set_size(candidate, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(candidate, 0, 0);
    lv_obj_set_style_bg_opa(candidate, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(candidate, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(candidate, 0, LV_PART_MAIN);
    lv_obj_clear_flag(candidate, LV_OBJ_FLAG_SCROLLABLE);

    int y = 4;
    auto add_line = [&](const char *text, uint32_t color, const lv_font_t *font) {
        lv_obj_t *label = lv_label_create(candidate);
        if (!label) return false;
        lv_label_set_text(label, text);
        lv_obj_set_pos(label, 8, y);
        lv_obj_set_width(label, 300);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_obj_update_layout(label);
        y += lv_obj_get_height(label) + 3;
        return true;
    };

    if (!add_line("Help", 0x58A6FF, launcher_fonts().get(
            "Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD)) ||
        !add_line("Screenshot: PrtSc / Ctrl+Alt+S", 0xCCCCCC, &lv_font_montserrat_12) ||
        !add_line("  Saved to ~/Pictures/Screenshots", 0x888888, &lv_font_montserrat_10) ||
        !add_line("Home: Hold ESC 3s", 0xCCCCCC, &lv_font_montserrat_12) ||
        !add_line("Navigate: Arrow keys / OK / ESC", 0xCCCCCC, &lv_font_montserrat_12) ||
        !add_line("WiFi: Setting > WiFi > Scan", 0xCCCCCC, &lv_font_montserrat_12))
        { lv_obj_delete(candidate); return false; }

    lv_obj_t *hint = lv_label_create(candidate);
    if (!hint) { lv_obj_delete(candidate); return false; }
    lv_label_set_text(hint, "ESC: back");
    lv_obj_set_pos(hint, 8, access.content_height() - 14);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, LV_PART_MAIN);

    lv_obj_t *child = lv_obj_get_child(container, 0);
    while (child) {
        if (child == candidate) {
            child = lv_obj_get_child(container, 1);
        } else {
            lv_obj_delete(child);
            child = lv_obj_get_child(container, 0);
        }
    }
    while ((child = lv_obj_get_child(candidate, 0)) != nullptr)
        lv_obj_set_parent(child, container);
    lv_obj_delete(candidate);
    return true;
}

void Help::handle_key(UISetupPage &page, uint32_t key)
{
    if (key != KEY_ESC && key != KEY_LEFT) return;
    SetupPageAccess access(page);
    if (!access.leave_help()) return;
    access.play_back();
    if (access.content_container()) access.build_sub_view();
}

void ExtPort::append(UISetupPage &page, std::vector<MenuItem> &menu)
{
    UISetupPage *page_ptr = &page;
    MenuItem item;
    item.label = "ExtPort";
    SetupPageAccess access(page);
    bool usb_enabled = access.gpio_get("extport_usb") == 1;
    bool output_enabled = access.gpio_get("extport_5vout") == 1;
    item.sub_items = {
        {"GROVE5V", true, usb_enabled, [page_ptr]() {
            apply_extport_toggle(*page_ptr, 0, "extport_usb");
        }},
        {"EXT5V", true, output_enabled, [page_ptr]() {
            apply_extport_toggle(*page_ptr, 1, "extport_5vout");
        }},
    };
    menu.push_back(item);
}

void Ethernet::append(UISetupPage &page, std::vector<MenuItem> &menu)
{
    UISetupPage *page_ptr = &page;
    MenuItem item;
    item.label = "Ethernet";
    item.sub_items = {
        {"IP: --", false, false, nullptr},
        {"Gateway: --", false, false, nullptr},
        {"MAC: --", false, false, nullptr},
    };
    item.on_enter = [page_ptr]() { Ethernet::refresh_info(*page_ptr); };
    menu.push_back(item);
}

void Ethernet::refresh_info(UISetupPage &page)
{
    MenuItem *item = SetupPageAccess(page).find_menu("Ethernet");
    if (!item || item->sub_items.size() < 3)
        return;
    std::string data;
    bool loaded = false;
    try {
        cp0_signal_osinfo_api({"NetworkDefaultInfoRead"},
            [&](int code, std::string value) {
                if (code == 0) { data = std::move(value); loaded = true; }
            });
    } catch (...) {
        loaded = false;
    }
    if (!loaded) return;
    const auto info = system_page::parse_network_info(data);
    item->sub_items[0].label = "IP: " + info.ip;
    item->sub_items[1].label = "GW: " + info.gateway;
    item->sub_items[2].label = "MAC: " + info.mac;
}

void Account::append(UISetupPage &page, std::vector<MenuItem> &menu)
{
    UISetupPage *page_ptr = &page;
    MenuItem item;
    item.label = "Account";
    item.sub_items = {
        {"Username", false, false, nullptr},
        {"Password", false, false, nullptr},
        {"Hostname", false, false, nullptr},
    };
    item.on_enter = [page_ptr]() { Account::refresh_info(*page_ptr); };
    menu.push_back(item);
}

void Account::refresh_info(UISetupPage &page)
{
    MenuItem *item = SetupPageAccess(page).find_menu("Account");
    if (!item || item->sub_items.size() < 3)
        return;
    std::string data;
    bool loaded = false;
    try {
        cp0_signal_osinfo_api({"AccountInfoRead"},
            [&](int code, std::string value) {
                if (code == 0) { data = std::move(value); loaded = true; }
            });
    } catch (...) {
        loaded = false;
    }
    if (!loaded) return;
    const auto info = system_page::parse_account_info(data);
    item->sub_items[0].label = "User: " + info.username;
    item->sub_items[1].label = "Password: ****";
    item->sub_items[2].label = "Host: " + info.hostname;
}

void Update::append(UISetupPage &page, std::vector<MenuItem> &menu)
{
    UISetupPage *page_ptr = &page;
    MenuItem item;
    item.label = "Update";
    item.sub_items = {
        {"Update Launcher", false, false, [page_ptr]() { Update::update_launcher(*page_ptr); }},
        {"Version: --", false, false, nullptr},
        {"Build: --", false, false, nullptr},
    };
    item.on_enter = [page_ptr]() { Update::refresh_version_info(*page_ptr); };
    menu.push_back(item);
}

void Update::refresh_version_info(UISetupPage &page)
{
    MenuItem *item = SetupPageAccess(page).find_menu("Update");
    if (item && item->sub_items.size() >= 3) {
        cp0_signal_osinfo_api({"UpdateLauncherState"}, [&](int code, std::string state) {
            if (code != 0) return;
            const std::string label = system_page::launcher_state_label(state);
            if (!label.empty()) item->sub_items[0].label = label;
        });
        item->sub_items[1].label = system_page::version_label(LAUNCHER_VERSION);
        item->sub_items[2].label = system_page::build_label(
            LAUNCHER_BUILD_DATE, LAUNCHER_CHANNEL, LAUNCHER_GIT_COMMIT);
    }
}

void Update::update_launcher(UISetupPage &page)
{
    page.start_update_job("UpdateLauncherStart", 0);
}

} // namespace setting

void UISetupPage::stop_update_timer(bool cancel_job)
{
    const bool had_job = update_timer_ || !update_job_id_.empty();
    if (update_timer_) lv_timer_delete(update_timer_);
    update_timer_ = nullptr;
    if (cancel_job && !update_job_id_.empty())
        cp0_signal_osinfo_api({"UpdateJobCancel", update_job_id_}, nullptr);
    update_job_id_.clear();
    if (had_job) launcher_toast().hide();
}

void UISetupPage::start_update_job(const char *command, int item_index)
{
    if (update_timer_ || !command) return;
    MenuItem *menu = setting::SetupPageAccess(*this).find_menu("Update");
    if (!menu || item_index < 0 || item_index >= static_cast<int>(menu->sub_items.size())) return;
    const std::string running_label = system_page::update_job_label(0, "running");
    launcher_toast().show_persistent(running_label.c_str());
    lv_refr_now(nullptr);
    try {
        cp0_signal_osinfo_api({command}, [&](int code, std::string id) {
            if (code == 0 && !id.empty()) update_job_id_ = std::move(id);
        });
    } catch (...) {
        update_job_id_.clear();
    }
    if (update_job_id_.empty()) {
        launcher_toast().show("Launcher update unavailable");
        return;
    }
    update_timer_ = lv_timer_create(update_timer_cb, 500, this);
    if (!update_timer_) {
        stop_update_timer();
        launcher_toast().show("Update status unavailable");
    }
}

void UISetupPage::update_timer_cb(lv_timer_t *timer) noexcept
{
    auto *page = timer ? static_cast<UISetupPage *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!page || !page->lifecycle_.active()) return;
    try {
        std::string state;
        int code = -1;
        cp0_signal_osinfo_api({"UpdateJobStatus", page->update_job_id_},
            [&](int result, std::string payload) { code = result; state = std::move(payload); });
        if (code == 0 && state == "running") return;
        const std::string result_label = system_page::update_job_label(code, state);
        page->stop_update_timer(false);
        launcher_toast().show(result_label.c_str());
    } catch (...) {
        page->stop_update_timer();
        launcher_toast().show("Update status unavailable");
    }
}
