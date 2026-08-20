#ifndef LAUNCH_WIZARD_APPLICATION_H
#define LAUNCH_WIZARD_APPLICATION_H

int lvgl_main(void);
void launch_wizard_register_event(void);
bool launch_wizard_ui_setup(void);
bool launch_wizard_should_quit(void);
void launch_wizard_ui_teardown(void);

// Returns true if the first-boot OOBE wizard should be shown on this device.
// Distinguishes a factory (unconfigured) image from a device the user already
// configured (e.g. CardputerZero Lite flashed with Raspberry Pi Imager).
bool launch_wizard_should_run(void);
int launch_wizard_finish_configured_system(void);

// Shows the keyboard tutorial before the OOBE decision on every non-test
// launch.
void launch_wizard_run_keyboard_guide(void);

#endif  // LAUNCH_WIZARD_APPLICATION_H
