#ifndef LAUNCH_WIZARD_FIRST_BOOT_POLICY_H
#define LAUNCH_WIZARD_FIRST_BOOT_POLICY_H

namespace launch_wizard {

// Snapshot of the on-disk first-boot signals, gathered by the caller so the
// decision itself stays a pure, unit-testable function.
struct FirstBootState {
    // /var/lib/applaunch/run-oobe: dropped by APPLaunch's "Run Setup Wizard"
    // settings entry to replay the OOBE on demand.
    bool rearm_marker = false;
    // /var/lib/LaunchWizard/run-oobe: baked into every factory image by pi-gen.
    bool factory_marker = false;
    // The UID 1000 user's shadow entry holds a real "$..." hash, meaning the
    // account was configured (Raspberry Pi Imager, a finished OOBE, ...).
    bool user_has_password = false;
    // Legacy piwiz/lightdm first-boot autologin is still armed.
    bool legacy_piwiz_active = false;
};

// A user-requested re-run always shows the wizard. The factory marker only
// shows it while no account has been configured yet: a device provisioned
// through Raspberry Pi Imager already has a password, so first boot must skip
// straight to the launcher.
bool should_run_wizard(const FirstBootState &state);

// The keyboard guide runs exactly once per device, before and independently of
// the OOBE wizard. A missing binary keeps the marker so a later boot (e.g.
// after the package lands) can still show the guide.
bool should_run_keyboard_guide(bool marker_present, bool binary_present);

}  // namespace launch_wizard

#endif  // LAUNCH_WIZARD_FIRST_BOOT_POLICY_H
