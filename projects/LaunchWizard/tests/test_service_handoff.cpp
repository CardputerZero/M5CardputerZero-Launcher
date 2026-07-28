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
        [&calls](const std::vector<std::string> &args) {
            calls.push_back(joined(args));
            return HandoffCommandResult{};
        });
    expect(error.empty(), "next-boot APPLaunch enable returned an error");
    expect(calls.size() == 1, "next-boot enable must execute exactly one command");
    if (calls.size() == 1) {
        expect(calls[0] == "systemctl --global enable APPLaunch.service",
               "next-boot enable did not use global user-service configuration");
        expect(calls[0].find("--now") == std::string::npos,
               "configuration phase attempted to start APPLaunch");
    }

    calls.clear();
    error = enable_applaunch_after_reboot(
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
    expect(calls.size() == 7, "successful handoff did not run all steps");
    if (calls.size() == 7) {
        expect(calls[4].find("enable --now APPLaunch.service") != std::string::npos,
               "APPLaunch was not started while being enabled");
        expect(calls[5].find("is-active --quiet APPLaunch.service") != std::string::npos,
               "APPLaunch active state was not verified");
        expect(calls[6] == "systemctl disable --now LaunchWizard.service",
               "LaunchWizard was not stopped last");
    }

    for (size_t failed_step = 0; failed_step < 6; ++failed_step) {
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
            if (calls.size() == 7)
                return HandoffCommandResult{1, "disable failed"};
            return HandoffCommandResult{};
        });
    expect(error.find("APPLaunch is active") != std::string::npos,
           "Wizard stop failure did not preserve handoff state in its diagnostic");

    return passed;
}
