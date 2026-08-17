#include "application.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char *argv[])
{
    // Allow forcing the OOBE for previews/demos on an already-configured device
    // (e.g. LAUNCH_WIZARD_FORCE=1 or passing --test/--force).
    const char *force_env = getenv("LAUNCH_WIZARD_FORCE");
    bool force = (force_env && force_env[0] && strcmp(force_env, "0") != 0);
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--test") == 0 || strcmp(argv[i], "--force") == 0) {
            force = true;
        } else if (strcmp(argv[i], "--preview-configuring") == 0) {
            force = true;
            setenv("LAUNCH_WIZARD_PREVIEW", "configuring", 1);
        } else if (strcmp(argv[i], "--preview-restart") == 0) {
            force = true;
            setenv("LAUNCH_WIZARD_PREVIEW", "restart", 1);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--test|--preview-configuring|--preview-restart]\n", argv[0]);
            printf("  --test  show the setup wizard on an already-configured device\n");
            return 0;
        } else {
            fprintf(stderr, "LaunchWizard: unknown option: %s\n", argv[i]);
            fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
            return 2;
        }
    }

    if (force)
        printf("LaunchWizard: test mode, bypassing first-boot detection\n");

    // The keyboard tutorial runs first and exactly once per device, whether
    // the OOBE that follows is shown or skipped (Imager-provisioned devices).
    if (!force)
        launch_wizard_run_keyboard_guide_once();

    if (!force && !launch_wizard_should_run()) {
        printf("LaunchWizard: first-boot desktop is not active, starting APPLaunch\n");
        const int handoff_result = launch_wizard_finish_configured_system();
        if (handoff_result == 0) return 0;

        fprintf(stderr,
                "LaunchWizard: APPLaunch handoff failed; keeping the wizard visible\n");
        return lvgl_main();
    }

    return lvgl_main();
}
