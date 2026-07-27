#ifndef LAUNCH_WIZARD_WIZARD_SERVICE_H
#define LAUNCH_WIZARD_WIZARD_SERVICE_H

#include "wizard_model.h"

#include <string>
#include <vector>
#include <functional>

namespace launch_wizard {

class WizardService {
public:
    static std::vector<WifiNetwork> scan_wifi();
    static WifiConnectionStatus read_wifi_status();
    static std::string connect_wifi(const std::string &ssid,
                                    const std::string &password,
                                    std::string *connected_ip = nullptr);
    static std::string set_manual_time(const std::string &date,
                                       const std::string &time);
    static std::string apply(const WizardModel &model,
                             const std::function<bool(const std::string &)> &progress);
    static std::string reboot();
    static bool should_run();
    static int finish_configured_system();
};

}  // namespace launch_wizard

#endif
