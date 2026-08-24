#include "settings_page.hpp"

#include "cp0_lvgl_app.h"
#include "settings_adb_guide_page.hpp"
#include "settings_battery_calibration_page.hpp"
#include "settings_bluetooth_connected_devices_page.hpp"
#include "settings_bluetooth_scan_page.hpp"
#include "settings_brightness_page.hpp"
#include "settings_camera_resolution_page.hpp"
#include "settings_confirmation_page.hpp"
#include "settings_rtc_page.hpp"
#include "settings_screen_timeout_page.hpp"
#include "settings_sound_card_page.hpp"
#include "settings_submenu_page.hpp"
#include "settings_value_page.hpp"
#include "settings_volume_page.hpp"
#include "settings_wifi_page.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace {

bool wifi_power_state = false;
bool wifi_power_pending = false;
std::recursive_mutex wifi_state_mutex;

bool query_wifi_power(bool &enabled)
{
    auto result = std::make_shared<std::atomic<int>>(-1);
    try {
        cp0_signal_wifi_api({"RadioEnabled"},
                            [result](int code, std::string) {
                                result->store(code, std::memory_order_release);
                            });
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

        bool enabled = false;
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

        bool enabled = false;
        const bool success = query_wifi_power(enabled);
        std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
        if (!wifi_power_pending && success) wifi_power_state = enabled;
        std::get<0>(*result) = wifi_power_state;
    } else if (cmd == SettingApiActivate) {
        bool next = false;
        {
            std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
            if (wifi_power_pending) return;
            next = !wifi_power_state;
            wifi_power_state = next;
            wifi_power_pending = true;
        }

        try {
            cp0_signal_wifi_api({"RadioSetEnabled", next ? "on" : "off"},
                                [next](int code, std::string) {
                                    std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
                                    wifi_power_pending = false;
                                    if (code != 0) wifi_power_state = !next;
                                });
        } catch (...) {
            std::lock_guard<std::recursive_mutex> lock(wifi_state_mutex);
            wifi_power_pending = false;
            wifi_power_state = !next;
        }
    }
}

Tree *&settings_tree_factory_context()
{
    static Tree *tree = nullptr;
    return tree;
}

static std::unique_ptr<DComponens::LvglComponensBase> roller_page_factory(lv_obj_t *parent,
                                                                          const NodeIter &page_node,
                                                                          std::function<void()> on_back)
{
#ifdef LAUNCHER_BUILD
    if (page_node->label == "Launcher") {
        Tree *tree = settings_tree_factory_context();
        if (tree) {
            tree->erase_children(page_node);
            std::size_t app_count = 0;
            const AppDescriptor *apps = launcher_app_registry_entries(&app_count);
            for (std::size_t index = 0; index < app_count; ++index) {
                const AppDescriptor &desc = apps[index];
                if (!desc.configurable || !desc.label) continue;
                tree->append_child(
                    page_node,
                    SettingEntry{std::string(desc.label), launcher_app_setting_api(desc), true});
            }
        }
    }
#endif
    return std::make_unique<LvSettingRollerPage2>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> roller_page3_factory(lv_obj_t *parent,
                                                                           const NodeIter &page_node,
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

static std::unique_ptr<DComponens::LvglComponensBase> volume_page3_factory(lv_obj_t *parent,
                                                                           const NodeIter &page_node,
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

static std::unique_ptr<DComponens::LvglComponensBase> rtc_page3_factory(lv_obj_t *parent,
                                                                        const NodeIter &page_node,
                                                                        std::function<void()> on_back)
{
    return std::make_unique<LvSettingRtcPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> confirm_page3_factory(lv_obj_t *parent,
                                                                            const NodeIter &page_node,
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
    return std::make_unique<LvSettingWifiScanPage3>(
        parent, page_node, std::move(on_back), false, enabled);
}

static std::unique_ptr<DComponens::LvglComponensBase> wifi_add_hidden_page_factory(lv_obj_t *parent,
                                                                                   const NodeIter &page_node,
                                                                                   std::function<void()> on_back)
{
    bool enabled = false;
    query_wifi_power(enabled);
    return std::make_unique<LvSettingWifiScanPage3>(
        parent, page_node, std::move(on_back), true, enabled);
}

static std::unique_ptr<DComponens::LvglComponensBase> adb_guide_page_factory(lv_obj_t *parent,
                                                                             const NodeIter &page_node,
                                                                             std::function<void()> on_back)
{
    return std::make_unique<LvSettingAdbGuidePage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> bluetooth_connected_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back)
{
    return std::make_unique<LvSettingBluetoothConnectedPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> bluetooth_scan_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back)
{
    return std::make_unique<LvSettingBluetoothScanPage3>(parent, page_node, std::move(on_back));
}

static std::unique_ptr<DComponens::LvglComponensBase> bluetooth_alias_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back)
{
    std::string alias = page_node->label;
    constexpr const char *prefix = "Alias: ";
    if (alias.rfind(prefix, 0) == 0) alias.erase(0, std::strlen(prefix));
    return std::make_unique<LvSettingBluetoothAliasPage3>(
        parent,
        page_node,
        std::move(on_back),
        std::move(alias),
        [page_node](std::string value) {
            page_node->label = "Alias: " + std::move(value);
        });
}

bool bluetooth_power_state = false;
bool bluetooth_discoverable_state = false;
bool bluetooth_named_only_state = true;
bool bluetooth_power_pending = false;
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
            if (code != 0 || !std::getline(input, powered_text, '\t') ||
                !std::getline(input, address, '\t') ||
                !std::getline(input, discoverable_text, '\t') ||
                !std::getline(input, alias, '\t'))
                return;
            if ((powered_text != "0" && powered_text != "1") ||
                (discoverable_text != "0" && discoverable_text != "1"))
                return;
            powered = powered_text == "1";
            discoverable = discoverable_text == "1";
            success = true;
        });
    } catch (...) {
    }
    return success;
}

void bluetooth_toggle_api(int cmd, void *data, bool &state, const char *command)
{
    std::lock_guard<std::recursive_mutex> state_lock(bluetooth_state_mutex);
    if (cmd == SettingApiReadFlag && data) {
        bool powered = bluetooth_power_state;
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
        auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
        bool powered = bluetooth_power_state;
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
        if (command &&
            ((std::strcmp(command, "BtPower") == 0 && bluetooth_power_pending) ||
             (std::strcmp(command, "BtDiscoverable") == 0 && bluetooth_discoverable_pending)))
            return;

        const bool next = !state;
        if (command) {
            try {
                if (std::strcmp(command, "BtPower") == 0) bluetooth_power_pending = true;
                if (std::strcmp(command, "BtDiscoverable") == 0)
                    bluetooth_discoverable_pending = true;

                state = next;
                cp0_signal_bt_api({command, next ? "1" : "0"},
                                  [command, next](int code, std::string) {
                                      std::lock_guard<std::recursive_mutex> state_lock(
                                          bluetooth_state_mutex);
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
                    bluetooth_power_state = !next;
                } else if (std::strcmp(command, "BtDiscoverable") == 0) {
                    bluetooth_discoverable_pending = false;
                    bluetooth_discoverable_state = !next;
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

        bool powered = bluetooth_power_state;
        bool status_powered = powered;
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

} // namespace

void UISettingTreePage::create_page_detail()
{
    settings_tree_factory_context() = &mode_tree;
    NodeIter root = mode_tree.set_head(SettingEntry{"Settings"});

#ifdef LAUNCHER_BUILD
    mode_tree.append_child(root, SettingEntry{"Launcher", roller_page_factory});
#else
    {
        NodeIter launcher = mode_tree.append_child(root, SettingEntry{"Launcher", roller_page_factory});
        mode_tree.append_child(launcher, SettingEntry{"Calculator", mork_api, true});
        mode_tree.append_child(launcher, SettingEntry{"LoRa", mork_api, true});
        mode_tree.append_child(launcher, SettingEntry{"IP_PANEL", mork_api, true});
        mode_tree.append_child(launcher, SettingEntry{"SSH", mork_api, true});
        mode_tree.append_child(launcher, SettingEntry{"Tank", mork_api, true});
    }
#endif
    {
        NodeIter boot = mode_tree.append_child(root, SettingEntry{"Boot", roller_page_factory});
        {
            NodeIter reboot = mode_tree.append_child(boot, SettingEntry{"Reboot", confirm_page3_factory});
            mode_tree.append_child(reboot, SettingEntry{"Yes"});
            mode_tree.append_child(reboot, SettingEntry{"No"});
        }
        {
            NodeIter shutdown = mode_tree.append_child(boot, SettingEntry{"Shutdown", confirm_page3_factory});
            mode_tree.append_child(shutdown, SettingEntry{"Yes"});
            mode_tree.append_child(shutdown, SettingEntry{"No"});
        }
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
        mode_tree.append_child(
            wifi,
            SettingEntry{"Add Hidden WiFi", wifi_add_hidden_page_factory, PageType::FullCustom});
    }

    {
        NodeIter speaker = mode_tree.append_child(root, SettingEntry{"Speaker", roller_page_factory});
        NodeIter volume = mode_tree.append_child(speaker, SettingEntry{"Volume", volume_page3_factory});
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
        NodeIter camera = mode_tree.append_child(root, SettingEntry{"Camera", roller_page_factory});
        NodeIter resolution = mode_tree.append_child(camera, SettingEntry{"Resolution", resolution_page3_factory});
        mode_tree.append_child(resolution, SettingEntry{"1280x720"});
        mode_tree.append_child(resolution, SettingEntry{"640x480"});
    }

    {
        NodeIter info = mode_tree.append_child(root, SettingEntry{"Info", roller_page_factory});
        mode_tree.append_child(info, SettingEntry{"Battery"});
        mode_tree.append_child(info, SettingEntry{"Temp"});
        mode_tree.append_child(info, SettingEntry{"Current"});
        mode_tree.append_child(info, SettingEntry{"Voltage"});
        NodeIter bq_calibrate = mode_tree.append_child(info, SettingEntry{"BQ Calibrate", bq_calibrate_page3_factory});
        mode_tree.append_child(bq_calibrate, SettingEntry{"Enter CAL"});
        mode_tree.append_child(bq_calibrate, SettingEntry{"CC Offset"});
        mode_tree.append_child(bq_calibrate, SettingEntry{"Board Offset"});
        mode_tree.append_child(bq_calibrate, SettingEntry{"Exit CAL"});
    }

    {
        NodeIter about = mode_tree.append_child(root, SettingEntry{"About", roller_page_factory});
        mode_tree.append_child(about, SettingEntry{"M5CardputerZero"});
        mode_tree.append_child(about, SettingEntry{"LVGL: 9.x"});
        mode_tree.append_child(about, SettingEntry{"Build"});
        mode_tree.append_child(about, SettingEntry{"Commit"});
    }

    {
        NodeIter help = mode_tree.append_child(root, SettingEntry{"Help", roller_page_factory});
        mode_tree.append_child(help, SettingEntry{"View Help"});
    }

    {
        NodeIter ext_port = mode_tree.append_child(root, SettingEntry{"ExtPort", roller_page_factory});
        mode_tree.append_child(ext_port, SettingEntry{"GROVE5V", mork_api, true});
        mode_tree.append_child(ext_port, SettingEntry{"EXT5V", mork_api, true});
    }

    {
        NodeIter developer = mode_tree.append_child(root, SettingEntry{"Developer", roller_page_factory});
        mode_tree.append_child(developer, SettingEntry{"ADB", mork_api, true});
        mode_tree.append_child(
            developer,
            SettingEntry{"ADB guide", adb_guide_page_factory, adb_guide_api, PageType::FullCustom});
    }

    {
        NodeIter rtc = mode_tree.append_child(root, SettingEntry{"RTC", roller_page_factory});
        mode_tree.append_child(rtc, SettingEntry{"NTP", mork_api, true});
        {
            NodeIter year = mode_tree.append_child(rtc, SettingEntry{"Year", rtc_page3_factory});
            append_numeric_options(mode_tree, year, 2000, 2099);
        }
        {
            NodeIter month = mode_tree.append_child(rtc, SettingEntry{"Month", rtc_page3_factory});
            append_numeric_options(mode_tree, month, 1, 12);
        }
        {
            NodeIter day = mode_tree.append_child(rtc, SettingEntry{"Day", rtc_page3_factory});
            append_numeric_options(mode_tree, day, 1, 31);
        }
        {
            NodeIter hour = mode_tree.append_child(rtc, SettingEntry{"Hour", rtc_page3_factory});
            append_numeric_options(mode_tree, hour, 0, 23);
        }
        {
            NodeIter minute = mode_tree.append_child(rtc, SettingEntry{"Minute", rtc_page3_factory});
            append_numeric_options(mode_tree, minute, 0, 59);
        }
        {
            NodeIter second = mode_tree.append_child(rtc, SettingEntry{"Second", rtc_page3_factory});
            append_numeric_options(mode_tree, second, 0, 59);
        }
        {
            NodeIter write_rtc = mode_tree.append_child(rtc, SettingEntry{"Write hardware RTC?", confirm_page3_factory});
            mode_tree.append_child(write_rtc, SettingEntry{"Yes"});
            mode_tree.append_child(write_rtc, SettingEntry{"No"});
        }
    }

    {
        NodeIter bluetooth = mode_tree.append_child(root, SettingEntry{"Bluetooth", roller_page_factory});
        mode_tree.append_child(bluetooth, SettingEntry{"Power", bluetooth_power_api, true});
        mode_tree.append_child(
            bluetooth,
            SettingEntry{"Alias: CardputerZero", bluetooth_alias_page_factory});
        mode_tree.append_child(bluetooth, SettingEntry{"Discoverable", bluetooth_discoverable_api, true});
        mode_tree.append_child(bluetooth, SettingEntry{"Named Only", bluetooth_named_only_api, true});
        mode_tree.append_child(
            bluetooth,
            SettingEntry{"Connected", bluetooth_connected_page_factory, PageType::FullCustom});
        mode_tree.append_child(
            bluetooth,
            SettingEntry{"Scan", bluetooth_scan_page_factory, PageType::FullCustom});
    }

    {
        NodeIter ethernet = mode_tree.append_child(root, SettingEntry{"Ethernet", roller_page_factory});
        mode_tree.append_child(ethernet, SettingEntry{"IP"});
        mode_tree.append_child(ethernet, SettingEntry{"Gateway"});
        mode_tree.append_child(ethernet, SettingEntry{"MAC"});
    }

    {
        NodeIter account = mode_tree.append_child(root, SettingEntry{"Account", roller_page_factory});
        mode_tree.append_child(account, SettingEntry{"Username"});
        mode_tree.append_child(account, SettingEntry{"Password"});
        mode_tree.append_child(account, SettingEntry{"Hostname"});
    }

    {
        NodeIter update = mode_tree.append_child(root, SettingEntry{"Update", roller_page_factory});
        mode_tree.append_child(update, SettingEntry{"Update Launcher"});
        mode_tree.append_child(update, SettingEntry{"Version"});
        mode_tree.append_child(update, SettingEntry{"Build"});
    }

    {
        NodeIter sound_card = mode_tree.append_child(root, SettingEntry{"SoundCard", roller_page_factory});
        mode_tree.append_child(sound_card, SettingEntry{"Open Mixer", soundcard_page4_factory, PageType::FullCustom});
    }
}

void UISettingTreePage::_back_home(void *data)
{
    auto *page = static_cast<UISettingTreePage *>(data);
    page->AnimateNextOut(nullptr);
    page->roller1_.reset();
    if (page && page->navigate_home) page->navigate_home();
}

void UISettingTreePage::LeaveNextPage()
{
    lv_async_call(_back_home, this);
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
    roller1_ = std::make_unique<LvSettingRoller>(ui_APP_Container, mode_tree.begin(), nullptr);
    AnimateNextIn([&]() {
        lv_group_add_obj(input_group(), roller1_->Get());
        lv_group_focus_obj(roller1_->Get());
    });
}

UISettingTreePage::UISettingTreePage() : AppPage()
{
    lv_obj_set_style_bg_color(screen(), lv_color_black(), LV_PART_MAIN);
    create_page_detail();
    LoadNextPage();
}

UISettingTreePage::~UISettingTreePage()
{
    roller1_.reset();
    if (settings_tree_factory_context() == &mode_tree) settings_tree_factory_context() = nullptr;
}
