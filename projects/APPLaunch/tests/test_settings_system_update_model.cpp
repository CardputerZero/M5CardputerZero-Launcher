#include "../main/ui/settings/settings_system_model.hpp"

#include <cassert>

int main()
{
    using namespace settings_system;

    assert(update_job_label(UpdateAction::UpdateLauncher, 0, "downloading") ==
           "Downloading launcher update...");
    assert(update_job_label(UpdateAction::UpdateLauncher, 0, "repairing") ==
           "Repairing package state...");
    assert(update_job_label(UpdateAction::UpdateLauncher, 0, "installing") ==
           "Installing launcher update...");
    assert(update_job_label(UpdateAction::UpdateLauncher, 0, "restarting") ==
           "Restarting Launcher...");
    assert(update_job_label(UpdateAction::UpdateLauncher, 0, "recovering:install") ==
           "Update failed; restoring previous version...");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1,
                            "failed:download-package:1") ==
           "Update download failed");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1,
                            "failed:incompatible:1") ==
           "No compatible update");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1,
                            "failed:version-not-newer:1") ==
           "Launcher is up to date");
    assert(update_job_label(UpdateAction::UpdateLauncher, 1,
                            "failed:rollback-unavailable:1") ==
           "Update blocked: no rollback");
    assert(launcher_state_label("downloading") ==
           "Downloading launcher update...");
    assert(launcher_state_label("failed:checksum") ==
           "Update verification failed");
}
