#include "service_handoff.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

std::string joined(const std::vector<std::string> &args)
{
    std::string result;
    for (const std::string &arg : args) {
        if (!result.empty()) result += ' ';
        result += arg;
    }
    return result;
}

}  // namespace

bool test_service_handoff()
{
    using namespace launch_wizard;
    bool passed = true;
    const auto expect = [&passed](bool condition, const char *message) {
        if (!condition) {
            std::cerr << message << '\n';
            passed = false;
        }
    };

    std::vector<std::string> calls;
    std::string error = enable_applaunch_after_reboot(
        "cardputer", 1000,
        [&calls](const std::vector<std::string> &args) {
            calls.push_back(joined(args));
            return HandoffCommandResult{};
        });
    expect(error.empty(), "next-boot APPLaunch enable returned an error");
    expect(calls.size() == 6, "next-boot enable did not run all user-scoped steps");
    if (calls.size() == 6) {
        expect(calls[0] == "systemctl --global disable APPLaunch.service",
               "next-boot enable did not remove legacy global configuration");
        expect(calls[5].find("systemctl --user enable APPLaunch.service") !=
                   std::string::npos,
               "next-boot enable was not scoped to the configured user");
        expect(calls[5].find("--now") == std::string::npos,
               "configuration phase attempted to start APPLaunch");
    }

    calls.clear();
    error = enable_applaunch_after_reboot(
        "cardputer", 1000,
        [&calls](const std::vector<std::string> &args) {
            calls.push_back(joined(args));
            return HandoffCommandResult{1, "injected enable failure"};
        });
    expect(error.find("injected enable failure") != std::string::npos,
           "next-boot enable failure discarded diagnostics");

    calls.clear();
    error = handoff_to_applaunch(
        "cardputer", 1000, [&calls](const std::vector<std::string> &args) {
            calls.push_back(joined(args));
            return HandoffCommandResult{};
        });
    expect(error.empty(), "successful handoff returned an error");
    expect(calls.size() == 8, "successful handoff did not run all steps");
    if (calls.size() == 8) {
        expect(calls[0] == "systemctl --global disable APPLaunch.service",
               "handoff did not remove legacy global configuration");
        expect(calls[5].find("enable --now APPLaunch.service") != std::string::npos,
               "APPLaunch was not started while being enabled");
        expect(calls[6].find("is-active --quiet APPLaunch.service") != std::string::npos,
               "APPLaunch active state was not verified");
        expect(calls[7] == "systemctl disable --now LaunchWizard.service",
               "LaunchWizard was not stopped last");
    }

    for (size_t failed_step = 0; failed_step < 7; ++failed_step) {
        calls.clear();
        error = handoff_to_applaunch(
            "cardputer", 1000,
            [&calls, failed_step](const std::vector<std::string> &args) {
                calls.push_back(joined(args));
                if (calls.size() == failed_step + 1)
                    return HandoffCommandResult{1, "injected failure"};
                return HandoffCommandResult{};
            });
        expect(!error.empty(), "handoff failure was not diagnosed");
        expect(error.find("injected failure") != std::string::npos,
               "command diagnostics were discarded");
        expect(calls.size() == failed_step + 1,
               "handoff continued after a required step failed");
        bool stopped_wizard = false;
        for (const std::string &call : calls)
            if (call == "systemctl disable --now LaunchWizard.service")
                stopped_wizard = true;
        expect(!stopped_wizard, "LaunchWizard was stopped before APPLaunch was active");
    }

    calls.clear();
    error = handoff_to_applaunch(
        "cardputer", 1000, [&calls](const std::vector<std::string> &args) {
            calls.push_back(joined(args));
            if (calls.size() == 8)
                return HandoffCommandResult{1, "disable failed"};
            return HandoffCommandResult{};
        });
    expect(error.find("APPLaunch is active") != std::string::npos,
           "Wizard stop failure did not preserve handoff state in its diagnostic");

    return passed;
}
