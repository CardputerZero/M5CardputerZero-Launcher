#include "settings_page.hpp"

#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "settings_adb_guide_page.hpp"
#include "settings_battery_info_page.hpp"
#include "settings_battery_calibration_page.hpp"
#include "settings_bluetooth_connected_devices_page.hpp"
#include "settings_bluetooth_scan_page.hpp"
#include "settings_brightness_page.hpp"
#include "settings_camera_resolution_page.hpp"
#include "settings_confirmation_page.hpp"
#include "settings_menu_roller.hpp"
#include "settings_rtc_page.hpp"
#include "settings_screen_timeout_page.hpp"
#include "settings_sound_card_page.hpp"
#include "settings_submenu_page.hpp"
#include "settings_system_page.hpp"
#include "settings_adapter.hpp"
#include "settings_static_info_page.hpp"
#include "settings_value_page.hpp"
#include "settings_volume_page.hpp"
#include "settings_wifi_page.hpp"
#include "settings_tree_types.hpp"
#include "settings_extport.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace {

bool wifi_power_state   = false;
bool wifi_power_pending = false;
std::recursive_mutex wifi_state_mutex;

bool query_wifi_power(bool &enabled)
{
    auto result = std::make_shared<std::atomic<int>>(-1);
    try {
        cp0_signal_wifi_api({"RadioEnabled"},
                            [result](int code, std::string) { result->store(code, std::memory_order_release); });
    } catch (...) {
        return false;
    }

    const int code = result->load(std::memory_order_acquire);
    if (code != 0 && code != 1) return false;
    enabled = code == 1;
    return true;
}

void wifi_power_api(int cmd, void *data)
{
    if (cmd == SettingApiReadFlag && data) {
        {
            std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
            if (wifi_power_pending) {
                *static_cast<bool *>(data) = wifi_power_state;
                return;
            }
        }

        bool enabled       = false;
        const bool success = query_wifi_power(enabled);
        std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
        if (!wifi_power_pending && success) wifi_power_state = enabled;
        *static_cast<bool *>(data) = wifi_power_state;
    } else if (cmd == SettingApiReadFlagTimeStart && data) {
        auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
        {
            std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
            if (wifi_power_pending) {
                std::get<0>(*result) = wifi_power_state;
                return;
            }
        }

        bool enabled       = false;
        const bool success = query_wifi_power(enabled);
        std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
        if (!wifi_power_pending && success) wifi_power_state = enabled;
        std::get<0>(*result) = wifi_power_state;
    } else if (cmd == SettingApiActivate) {
        bool next = false;
        {
            std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
            if (wifi_power_pending) return;
            next               = !wifi_power_state;
            wifi_power_state   = next;
            wifi_power_pending = true;
        }

        try {
            cp0_signal_wifi_api({"RadioSetEnabled", next ? "on" : "off"}, [next](int code, std::string) {
                std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
                wifi_power_pending = false;
                if (code != 0) wifi_power_state = !next;
            });
        } catch (...) {
            std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
            wifi_power_pending = false;
            wifi_power_state   = !next;
        }
    }
}

Tree *&settings_tree_factory_context()
{
    static Tree *tree = nullptr;
    return tree;
}

static std::unique_ptr<DComponens::LvglComponensBase> bluetooth_roller_page_factory(lv_obj_t *parent,
                                                                                    const NodeIter &page_node,
                                                                                    std::function<void()> on_back)
{
#ifdef LAUNCHER_BUILD
    if (page_node->label == "Launcher") {
        Tree *tree = settings_tree_factory_context();
        if (tree) {
            settings_t12b::populate_launcher_children(*tree, page_node);
        }
    }
#endif
    system("/usr/sbin/rfkill unblock bluetooth");
    return std::make_unique<LvSettingRollerPage2>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> roller_page_factory(lv_obj_t *parent, const NodeIter &page_node,
                                                                          std::function<void()> on_back)
{
#ifdef LAUNCHER_BUILD
    if (page_node->label == "Launcher") {
        Tree *tree = settings_tree_factory_context();
        if (tree) {
            settings_t12b::populate_launcher_children(*tree, page_node);
        }
    }
#endif
    return std::make_unique<LvSettingRollerPage2>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> roller_page3_factory(lv_obj_t *parent, const NodeIter &page_node,
                                                                           std::function<void()> on_back)
{
    return std::make_unique<LvSettingRollerPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> brightness_page3_factory(lv_obj_t *parent,
                                                                               const NodeIter &page_node,
                                                                               std::function<void()> on_back)
{
    return std::make_unique<LvSettingBrightnessPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> dark_time_page3_factory(lv_obj_t *parent,
                                                                              const NodeIter &page_node,
                                                                              std::function<void()> on_back)
{
    return std::make_unique<LvSettingDarkTimePage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> volume_page3_factory(lv_obj_t *parent, const NodeIter &page_node,
                                                                           std::function<void()> on_back)
{
    return std::make_unique<LvSettingVolumePage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> resolution_page3_factory(lv_obj_t *parent,
                                                                               const NodeIter &page_node,
                                                                               std::function<void()> on_back)
{
    return std::make_unique<LvSettingResolutionPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> bq_calibrate_page3_factory(lv_obj_t *parent,
                                                                                 const NodeIter &page_node,
                                                                                 std::function<void()> on_back)
{
    return std::make_unique<LvSettingBQCalibratePage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> rtc_page3_factory(lv_obj_t *parent, const NodeIter &page_node,
                                                                        std::function<void()> on_back)
{
    return std::make_unique<LvSettingRtcPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> confirm_page3_factory(lv_obj_t *parent, const NodeIter &page_node,
                                                                            std::function<void()> on_back)
{
    return std::make_unique<LvSettingConfirmPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> wifi_scan_page3_factory(lv_obj_t *parent,
                                                                              const NodeIter &page_node,
                                                                              std::function<void()> on_back)
{
    bool enabled = false;
    query_wifi_power(enabled);
    return std::make_unique<LvSettingWifiScanPage3>(parent, page_node, std::move(on_back), false, enabled);
}

static std::unique_ptr<DComponens::LvglComponensBase> wifi_add_hidden_page_factory(lv_obj_t *parent,
                                                                                   const NodeIter &page_node,
                                                                                   std::function<void()> on_back)
{
    bool enabled = false;
    query_wifi_power(enabled);
    return std::make_unique<LvSettingWifiScanPage3>(parent, page_node, std::move(on_back), true, enabled);
}

static std::unique_ptr<DComponens::LvglComponensBase> adb_guide_page_factory(lv_obj_t *parent,
                                                                             const NodeIter &page_node,
                                                                             std::function<void()> on_back)
{
    return std::make_unique<LvSettingAdbGuidePage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> bluetooth_connected_page_factory(lv_obj_t *parent,
                                                                                       const NodeIter &page_node,
                                                                                       std::function<void()> on_back)
{
    return std::make_unique<LvSettingBluetoothConnectedPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> bluetooth_scan_page_factory(lv_obj_t *parent,
                                                                                  const NodeIter &page_node,
                                                                                  std::function<void()> on_back)
{
    return std::make_unique<LvSettingBluetoothScanPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> bluetooth_alias_page_factory(lv_obj_t *parent,
                                                                                   const NodeIter &page_node,
                                                                                   std::function<void()> on_back)
{
    std::string alias            = page_node->label;
    constexpr const char *prefix = "Alias: ";
    if (alias.rfind(prefix, 0) == 0) alias.erase(0, std::strlen(prefix));
    return std::make_unique<LvSettingBluetoothAliasPage3>(
        parent, page_node, std::move(on_back), std::move(alias),
        [page_node](std::string value) { page_node->label = "Alias: " + std::move(value); });
}

bool bluetooth_power_state          = false;
bool bluetooth_discoverable_state   = false;
bool bluetooth_named_only_state     = true;
bool bluetooth_power_pending        = false;
bool bluetooth_discoverable_pending = false;
std::recursive_mutex bluetooth_state_mutex;

bool query_bluetooth_status(bool &powered, bool &discoverable)
{
    bool success = false;
    try {
        cp0_signal_bt_api({"BtStatus"}, [&](int code, std::string data) {
            std::istringstream input(data);
            std::string powered_text;
            std::string address;
            std::string discoverable_text;
            std::string alias;
            if (code != 0 || !std::getline(input, powered_text, '\t') || !std::getline(input, address, '\t') ||
                !std::getline(input, discoverable_text, '\t') || !std::getline(input, alias, '\t'))
                return;
            if ((powered_text != "0" && powered_text != "1") || (discoverable_text != "0" && discoverable_text != "1"))
                return;
            powered      = powered_text == "1";
            discoverable = discoverable_text == "1";
            success      = true;
        });
    } catch (...) {
    }
    return success;
}

void bluetooth_toggle_api(int cmd, void *data, bool &state, const char *command)
{
    std::lock_guard<std::recursive_mutex> state_lock(bluetooth_state_mutex);
    if (cmd == SettingApiReadFlag && data) {
        bool powered      = bluetooth_power_state;
        bool discoverable = bluetooth_discoverable_state;
        if (command &&
            !((std::strcmp(command, "BtPower") == 0 && bluetooth_power_pending) ||
              (std::strcmp(command, "BtDiscoverable") == 0 && bluetooth_discoverable_pending)) &&
            query_bluetooth_status(powered, discoverable)) {
            state = std::strcmp(command, "BtDiscoverable") == 0 ? discoverable : powered;
            if (std::strcmp(command, "BtPower") == 0)
                bluetooth_power_state = powered;
            else if (std::strcmp(command, "BtDiscoverable") == 0)
                bluetooth_discoverable_state = discoverable;
        }
        *static_cast<bool *>(data) = state;
    } else if (cmd == SettingApiReadFlagTimeStart && data) {
        auto *result      = static_cast<SettingApiReadFlagTimeStartData *>(data);
        bool powered      = bluetooth_power_state;
        bool discoverable = bluetooth_discoverable_state;
        if (command &&
            !((std::strcmp(command, "BtPower") == 0 && bluetooth_power_pending) ||
              (std::strcmp(command, "BtDiscoverable") == 0 && bluetooth_discoverable_pending)) &&
            query_bluetooth_status(powered, discoverable)) {
            state = std::strcmp(command, "BtDiscoverable") == 0 ? discoverable : powered;
            if (std::strcmp(command, "BtPower") == 0)
                bluetooth_power_state = powered;
            else if (std::strcmp(command, "BtDiscoverable") == 0)
                bluetooth_discoverable_state = discoverable;
        }
        std::get<0>(*result) = state;
    } else if (cmd == SettingApiActivate) {
        if (command && ((std::strcmp(command, "BtPower") == 0 && bluetooth_power_pending) ||
                        (std::strcmp(command, "BtDiscoverable") == 0 && bluetooth_discoverable_pending)))
            return;

        const bool next = !state;
        if (command) {
            try {
                if (std::strcmp(command, "BtPower") == 0) bluetooth_power_pending = true;
                if (std::strcmp(command, "BtDiscoverable") == 0) bluetooth_discoverable_pending = true;

                state = next;
                cp0_signal_bt_api({command, next ? "1" : "0"}, [command, next](int code, std::string) {
                    std::lock_guard<std::recursive_mutex> state_lock(bluetooth_state_mutex);
                    if (std::strcmp(command, "BtPower") == 0) {
                        bluetooth_power_pending = false;
                        if (code != 0) bluetooth_power_state = !next;
                    } else if (std::strcmp(command, "BtDiscoverable") == 0) {
                        bluetooth_discoverable_pending = false;
                        if (code != 0) bluetooth_discoverable_state = !next;
                    }
                });
            } catch (...) {
                if (std::strcmp(command, "BtPower") == 0) {
                    bluetooth_power_pending = false;
                    bluetooth_power_state   = !next;
                } else if (std::strcmp(command, "BtDiscoverable") == 0) {
                    bluetooth_discoverable_pending = false;
                    bluetooth_discoverable_state   = !next;
                }
            }
        } else {
            state = next;
        }
    }
}

void bluetooth_power_api(int cmd, void *data)
{
    bluetooth_toggle_api(cmd, data, bluetooth_power_state, "BtPower");
}

void bluetooth_discoverable_api(int cmd, void *data)
{
    if (cmd == SettingApiActivate) {
        auto *page = static_cast<LvSettingRollerPage2 *>(data);
        std::lock_guard<std::recursive_mutex> state_lock(bluetooth_state_mutex);
        if (bluetooth_discoverable_pending) return;

        bool powered              = bluetooth_power_state;
        bool status_powered       = powered;
        bool ignored_discoverable = false;
        if (!bluetooth_power_pending && query_bluetooth_status(status_powered, ignored_discoverable)) {
            powered = status_powered;
        }
        if (bluetooth_power_pending || !powered) {
            if (page) page->show_power_warning();
            return;
        }
    }
    bluetooth_toggle_api(cmd, data, bluetooth_discoverable_state, "BtDiscoverable");
}

void bluetooth_named_only_api(int cmd, void *data)
{
    bluetooth_toggle_api(cmd, data, bluetooth_named_only_state, nullptr);
}

static void append_numeric_options(Tree &tree, const NodeIter &parent, int first, int last)
{
    for (int value = first; value <= last; ++value) tree.append_child(parent, SettingEntry{std::to_string(value)});
}

}  // namespace

void UISettingTreePage::create_page_detail()
{
    Tree &mode_tree                 = mode_tree_;
    settings_tree_factory_context() = &mode_tree;
    NodeIter root                   = mode_tree.set_head(SettingEntry{"Settings"});

#ifdef LAUNCHER_BUILD
    mode_tree.append_child(root, SettingEntry{"Launcher", roller_page_factory});
#endif
    {
        NodeIter boot = mode_tree.append_child(root, SettingEntry{"Boot", roller_page_factory});
        settings_t12b::append_boot_children(mode_tree, boot, confirm_page3_factory);
    }

    {
        NodeIter screen = mode_tree.append_child(root, SettingEntry{"Screen", roller_page_factory});
        {
            NodeIter brightness = mode_tree.append_child(screen, SettingEntry{"Brightness", brightness_page3_factory});
            mode_tree.append_child(brightness, SettingEntry{"100%"});
            mode_tree.append_child(brightness, SettingEntry{"75%"});
            mode_tree.append_child(brightness, SettingEntry{"50%"});
            mode_tree.append_child(brightness, SettingEntry{"25%"});
        }
        {
            NodeIter dark_time = mode_tree.append_child(screen, SettingEntry{"DarkTime", dark_time_page3_factory});
            mode_tree.append_child(dark_time, SettingEntry{"Never"});
            mode_tree.append_child(dark_time, SettingEntry{"10S"});
            mode_tree.append_child(dark_time, SettingEntry{"30S"});
            mode_tree.append_child(dark_time, SettingEntry{"60S"});
            mode_tree.append_child(dark_time, SettingEntry{"300S"});
        }
    }

    {
        NodeIter wifi = mode_tree.append_child(root, SettingEntry{"WiFi", roller_page_factory});
        mode_tree.append_child(wifi, SettingEntry{"Power", wifi_power_api, true});
        mode_tree.append_child(wifi, SettingEntry{"Scan", wifi_scan_page3_factory, PageType::FullCustom});
        mode_tree.append_child(wifi,
                               SettingEntry{"Add Hidden WiFi", wifi_add_hidden_page_factory, PageType::FullCustom});
    }

    {
        NodeIter speaker = mode_tree.append_child(root, SettingEntry{"Speaker", roller_page_factory});
        NodeIter volume  = mode_tree.append_child(speaker, SettingEntry{"Volume", volume_page3_factory});
        mode_tree.append_child(volume, SettingEntry{"100%"});
        mode_tree.append_child(volume, SettingEntry{"90%"});
        mode_tree.append_child(volume, SettingEntry{"80%"});
        mode_tree.append_child(volume, SettingEntry{"70%"});
        mode_tree.append_child(volume, SettingEntry{"60%"});
        mode_tree.append_child(volume, SettingEntry{"50%"});
        mode_tree.append_child(volume, SettingEntry{"40%"});
        mode_tree.append_child(volume, SettingEntry{"30%"});
        mode_tree.append_child(volume, SettingEntry{"20%"});
        mode_tree.append_child(volume, SettingEntry{"10%"});
        mode_tree.append_child(volume, SettingEntry{"0%"});
    }
    {
        NodeIter info = mode_tree.append_child(root, SettingEntry{"Battery", roller_page_factory});
        mode_tree.append_child(info, SettingEntry{"Info", settings_battery_info_page_factory, PageType::FullCustom});
#if 0
        NodeIter bq_calibrate = mode_tree.append_child(info, SettingEntry{"BQ Calibrate", bq_calibrate_page3_factory});
        mode_tree.append_child(bq_calibrate, SettingEntry{"Enter CAL"});
        mode_tree.append_child(bq_calibrate, SettingEntry{"CC Offset"});
        mode_tree.append_child(bq_calibrate, SettingEntry{"Board Offset"});
        mode_tree.append_child(bq_calibrate, SettingEntry{"Exit CAL"});
#endif
    }

    {
        NodeIter about = mode_tree.append_child(root, SettingEntry{"About", roller_page_factory});
        mode_tree.append_child(about, SettingEntry{"OS", settings_system_info_page3_factory, PageType::FullCustom});
        mode_tree.append_child(about, SettingEntry{"APPLaunch", settings_update_page_factory, PageType::FullCustom});
    }

    {
        mode_tree.append_child(root, SettingEntry{"Help", settings_t12b_help_page_factory, PageType::FullCustom});
    }

    {
        NodeIter ext_port = mode_tree.append_child(root, SettingEntry{"ExtPort", roller_page_factory});
        mode_tree.append_child(ext_port, SettingEntry{"GROVE5V",std::bind(&ext_port_com, "GROVE5V", std::placeholders::_1, std::placeholders::_2), true});
        mode_tree.append_child(ext_port, SettingEntry{"EXT5V",std::bind(&ext_port_com, "EXT5V", std::placeholders::_1, std::placeholders::_2), true});
    }

    {
        NodeIter developer = mode_tree.append_child(root, SettingEntry{"Developer", roller_page_factory});
        SettingEntry adb_entry{"ADB", LvSettingAdbGuidePage3::toggle_setting, true};
        adb_entry.status_read_policy = SettingStatusReadPolicy::Direct;
        mode_tree.append_child(developer, std::move(adb_entry));
        mode_tree.append_child(developer, SettingEntry{"ADB guide", adb_guide_page_factory, PageType::FullCustom});
    }
    {
        NodeIter bluetooth = mode_tree.append_child(root, SettingEntry{"Bluetooth", bluetooth_roller_page_factory});
        mode_tree.append_child(bluetooth, SettingEntry{"Power", bluetooth_power_api, true});
        mode_tree.append_child(bluetooth, SettingEntry{"Alias: CardputerZero", bluetooth_alias_page_factory});
        mode_tree.append_child(bluetooth, SettingEntry{"Discoverable", bluetooth_discoverable_api, true});
        mode_tree.append_child(bluetooth, SettingEntry{"Named Only", bluetooth_named_only_api, true});
        mode_tree.append_child(bluetooth,
                               SettingEntry{"Connected", bluetooth_connected_page_factory, PageType::FullCustom});
        mode_tree.append_child(bluetooth, SettingEntry{"Scan", bluetooth_scan_page_factory, PageType::FullCustom});
    }

    {
        mode_tree.append_child(root, SettingEntry{"Ethernet", settings_ethernet_page_factory, PageType::FullCustom});
    }

    {
        mode_tree.append_child(root, SettingEntry{"Account", settings_account_page_factory, PageType::FullCustom});
    }

    {
        NodeIter sound_card = mode_tree.append_child(root, SettingEntry{"SoundCard", roller_page_factory});
        mode_tree.append_child(sound_card, SettingEntry{"Open Mixer", soundcard_page4_factory, PageType::FullCustom});
    }
}

void UISettingTreePage::back_home(void *data)
{
    auto *page = static_cast<UISettingTreePage *>(data);
    if (!page) return;

    page->AnimateNextOut(nullptr);
    page->roller_.reset();
    if (page->navigate_home) page->navigate_home();
}

void UISettingTreePage::LeaveNextPage()
{
    if (lv_async_call(back_home, this) != LV_RESULT_OK) back_home(this);
}

void UISettingTreePage::AnimateNextIn(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void UISettingTreePage::AnimateNextOut(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void UISettingTreePage::LoadNextPage()
{
    roller_ = std::make_unique<LvSettingRoller>(ui_APP_Container, mode_tree_.begin(),
                                                std::bind(&UISettingTreePage::LeaveNextPage, this));
    AnimateNextIn([this]() {
        if (!roller_) return;
        lv_group_add_obj(input_group(), roller_->Get());
        lv_group_focus_obj(roller_->Get());
    });
}

UISettingTreePage::UISettingTreePage() : AppPage()
{
    set_page_title("Settings");
    lv_obj_set_style_bg_color(screen(), lv_color_black(), LV_PART_MAIN);
    create_page_detail();
    LoadNextPage();
}

UISettingTreePage::~UISettingTreePage()
{
    lv_async_call_cancel(back_home, this);
    roller_.reset();
    if (settings_tree_factory_context() == &mode_tree_) settings_tree_factory_context() = nullptr;
}
