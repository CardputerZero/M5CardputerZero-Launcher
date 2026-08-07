#include "../main/ui/model/system_page_model.hpp"

#include <cassert>
#include <string>

int main()
{
    using namespace system_page;

    std::string current = "stable";
    assert(!commit_if_success(false, std::string("partial"), current));
    assert(current == "stable");
    assert(commit_if_success(true, std::string("committed"), current));
    assert(current == "committed");

    NetworkInfo network = parse_network_info("192.0.2.5\n192.0.2.1\naa:bb:cc:dd:ee:ff\n");
    assert(network.ip == "192.0.2.5");
    assert(network.gateway == "192.0.2.1");
    assert(network.mac == "aa:bb:cc:dd:ee:ff");

    network = parse_network_info("10.0.0.2\r\n\r\n");
    assert(network.ip == "10.0.0.2");
    assert(network.gateway == "--");
    assert(network.mac == "--");

    AccountInfo account = parse_account_info("alice\r\ncardputer\r\n");
    assert(account.username == "alice");
    assert(account.hostname == "cardputer");

    account = parse_account_info("");
    assert(account.username == "--");
    assert(account.hostname == "--");

    assert(version_label("1.2.3") == "Version: 1.2.3");
    assert(version_label("") == "Version: --");
    assert(build_label("2026-07-28", "stable", "abc123") ==
           "Build: 2026-07-28 stable (abc123)");
    assert(std::string(update_request(UpdateAction::CheckSystem)) == "AptUpdateStart");
    assert(std::string(update_request(UpdateAction::UpdateLauncher)) ==
           "UpdateLauncherStart");
    assert(update_job_label(UpdateAction::CheckSystem, 0, "running").empty());
    assert(update_job_label(UpdateAction::CheckSystem, 0, "succeeded:completed") ==
           "System check complete");
    assert(update_job_label(UpdateAction::CheckSystem, 1, "failed:apt-update:1") ==
           "System check failed");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1,
                            "failed:incompatible:1") == "No compatible update");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1,
                            "failed:version-not-newer:1") == "Launcher is up to date");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1,
                            "failed:download-package:1") == "Update download failed");
    assert(launcher_state_label("downloading") == "Updating: downloading");
    assert(launcher_state_label("succeeded:0.6.32") == "Launcher is up to date");
    assert(launcher_state_label("failed:incompatible") == "No compatible update");

    assert(extport_toggle_value(false, true, true));
    assert(!extport_toggle_value(true, false, true));
    assert(extport_toggle_value(true, false, false));
    assert(!extport_toggle_value(false, true, false));
}
