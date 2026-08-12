#pragma once

#include "keyboard_input.h"
#include "wizard_model.h"

namespace launch_wizard {

inline cp0_keyboard_input_context_t wizard_input_context(const WizardModel &model)
{
    bool editing = model.screen == Screen::Hostname || model.screen == Screen::Account ||
                   model.screen == Screen::ManualTime;
    if (model.screen == Screen::WifiPassword && !model.wifi_connected)
        editing = true;
    if (model.screen == Screen::EthernetConfig && !model.ethernet_dhcp &&
        model.ethernet_focus != 0)
        editing = true;
    return editing ? KBD_INPUT_CONTEXT_TEXT : KBD_INPUT_CONTEXT_NAVIGATION;
}

}  // namespace launch_wizard
