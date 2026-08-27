#ifndef LAUNCH_WIZARD_WIZARD_SERVICE_H
#define LAUNCH_WIZARD_WIZARD_SERVICE_H

#include "wizard_model.h"

#include <string>
#include <vector>
#include <functional>

namespace launch_wizard {

struct WifiScanResult {
    std::vector<WifiNetwork> networks;
    int error = 0;
};

struct ProgressEvent {
    int step;
    int total;
    std::string label;
};

class WizardService {
public:
    static WifiScanResult scan_wifi();
    static WifiConnectionStatus read_wifi_status();
    static std::string connect_wifi(const std::string &ssid,
                                    const std::string &password,
                                    std::string *connected_ip = nullptr,
                                    bool hidden = false);
    static std::string set_manual_time(const std::string &date,
                                       const std::string &time);
    static std::string apply(const WizardModel &model,
                             const std::function<void(const ProgressEvent &)> &progress,
                             const std::function<bool()> &cancelled = {});
    static std::string reboot();
    static bool should_run();
    static int finish_configured_system();
    // Consumes pi-gen's one-shot marker and runs the keyboard tutorial before
    // the OOBE decision. Returns once the guide exits or fails to start.
    static void run_keyboard_guide();
};

}  // namespace launch_wizard

#endif
