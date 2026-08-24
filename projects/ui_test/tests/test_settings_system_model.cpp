#include "../main/ui/settings_system_model.hpp"

#include <cassert>
#include <cerrno>
#include <string>

int main()
{
    using namespace settings_system;

    NetworkInfo network = parse_network_info(
        "192.0.2.5\n192.0.2.1\naa:bb:cc:dd:ee:ff\r\n");
    assert(network.ip == "192.0.2.5");
    assert(network.gateway == "192.0.2.1");
    assert(network.mac == "aa:bb:cc:dd:ee:ff");

    network = parse_network_info("10.0.0.2\r\n\r\n");
    assert(network.ip == "10.0.0.2");
    assert(network.gateway == "--");
    assert(network.mac == "--");

    NetworkInfo strict_network;
    assert(parse_network_info_strict("192.0.2.5\n192.0.2.1\nmac", strict_network));
    assert(strict_network.mac == "mac");
    assert(!parse_network_info_strict("192.0.2.5\n192.0.2.1\nmac\n", strict_network));
    assert(!parse_network_info_strict("192.0.2.5\n192.0.2.1", strict_network));
    assert(!parse_network_info_strict("192.0.2.5\n192.0.2.1\nmac\nextra", strict_network));

    AccountInfo account = parse_account_info("alice\r\ncardputer\r\n");
    assert(account.username == "alice");
    assert(account.hostname == "cardputer");
    assert(parse_account_info("").username == "--");

    AccountInfo strict_account;
    assert(parse_account_info_strict("alice\ncardputer", strict_account));
    assert(!parse_account_info_strict("alice", strict_account));
    assert(!parse_account_info_strict("alice\ncardputer\nextra", strict_account));

    assert(version_label("1.2.3") == "Version: 1.2.3");
    assert(version_label("") == "Version: --");
    assert(build_label("2026-08-24", "stable", "abc123") ==
           "Build: 2026-08-24 stable (abc123)");
    assert(std::string(update_request(UpdateAction::CheckSystem)) == "AptUpdateStart");
    assert(std::string(update_request(UpdateAction::UpdateLauncher)) == "UpdateLauncherStart");
    assert(std::string(background_update_request(UpdateAction::CheckSystem)) ==
           "AptUpdateBackground");
    assert(std::string(background_update_request(UpdateAction::UpdateLauncher)) ==
           "UpdateLauncherBackground");

    assert(update_job_label(UpdateAction::CheckSystem, 0, "running") ==
           "Refreshing package lists...");
    assert(update_job_label(UpdateAction::UpdateLauncher, 0, "running") ==
           "Refreshing packages...\nChecking launcher update...\nLauncher may restart");
    assert(update_job_label(UpdateAction::CheckSystem, 0, "succeeded:completed") ==
           "Package lists refreshed");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1, "failed:incompatible:1") ==
           "No compatible update");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1, "failed:checksum:1") ==
           "Update verification failed");
    assert(update_phase_label(UpdateAction::UpdateLauncher, UpdatePhase::TimedOut) ==
           "Update timed out");
    assert(update_job_label(UpdateAction::UpdateLauncher, -ETIMEDOUT, "") ==
           "Update timed out");
    assert(backend_error_label(-1) == "Backend unavailable (-1)");
    assert(backend_error_label(-1, "osinfo service unavailable") ==
           "Backend error: osinfo service unavailable");
    assert(update_phase_label(UpdateAction::UpdateLauncher, UpdatePhase::Cancelled) ==
           "Update cancelled");
    assert(launcher_state_label("succeeded:0.6.32") == "Launcher is up to date");
    assert(launcher_state_label("failed:incompatible") == "No compatible update");
    assert(password_unsupported_label() == "Password changes are not supported");

    system_page::NetworkInfo compatibility = system_page::parse_network_info("ip\ngw\nmac");
    assert(compatibility.ip == "ip");
}
