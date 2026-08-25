#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "setup_page_access.hpp"

namespace setting {

void build_menu(UISetupPage &page)
{
    SetupPageAccess access(page);
    auto &menu = access.menus();
    std::vector<MenuItem> candidate;
    Launcher::append(page, candidate);
    Boot::append(page, candidate);
    access.screen().append(page, candidate);
    access.wifi().append(page, candidate);
    access.speaker().append(page, candidate);
    access.info().append(page, candidate);
    About::append(page, candidate);
    Help::append(page, candidate);
    ExtPort::append(page, candidate);
    access.developer().append(page, candidate);
    {
        const bool named_only = access.config_get_int("bt_named_only", 1) != 0;
        UISetupPage *page_ptr = &page;
        MenuItem m;
        m.label = "Bluetooth";
        m.sub_items = {
            {"Power", true, false, [page_ptr]() {
                SetupPageAccess access(*page_ptr);
                access.bluetooth().toggle_power(*page_ptr);
            }},
            {"Alias: CardputerZero", false, false, [page_ptr]() {
                SetupPageAccess access(*page_ptr);
                access.bluetooth().enter_alias(*page_ptr);
            }},
            {"Discoverable", true, false, [page_ptr]() {
                SetupPageAccess access(*page_ptr);
                access.bluetooth().toggle_discoverable(*page_ptr);
            }},
            {"Named Only", true, named_only, [page_ptr]() {
                SetupPageAccess access(*page_ptr);
                access.bluetooth().toggle_named_only(*page_ptr);
            }},
            {"Connected", false, false, [page_ptr]() {
                SetupPageAccess access(*page_ptr);
                access.bluetooth().enter_devices(*page_ptr);
            }},
            {"Scan", false, false, [page_ptr]() {
                SetupPageAccess access(*page_ptr);
                access.bluetooth().enter_scan(*page_ptr);
            }},
        };
        m.on_enter = [page_ptr]() {
            SetupPageAccess access(*page_ptr);
            access.bluetooth().refresh_status(*page_ptr);
        };
        candidate.push_back(std::move(m));
    }
    Ethernet::append(page, candidate);
    Account::append(page, candidate);
    Update::append(page, candidate);
    access.soundcard().append(page, candidate);
    menu.swap(candidate);
}

} // namespace setting
