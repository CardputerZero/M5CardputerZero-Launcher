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
    access.camera().append(page, candidate);
    access.info().append(page, candidate);
    About::append(page, candidate);
    Help::append(page, candidate);
    ExtPort::append(page, candidate);
    access.developer().append(page, candidate);
    access.bluetooth().append(page, candidate);
    Ethernet::append(page, candidate);
    Account::append(page, candidate);
    Update::append(page, candidate);
    access.soundcard().append(page, candidate);
    menu.swap(candidate);
}

} // namespace setting
