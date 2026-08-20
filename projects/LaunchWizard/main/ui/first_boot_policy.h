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
    // The UID 1000 user is still named after the factory default ("pi"). A
    // rename can only come from provisioning (Imager/userconf), so a changed
    // name alone means the device was configured.
    bool factory_username = false;
    // The UID 1000 user's shadow entry holds a real "$..." hash, meaning the
    // account was configured (Raspberry Pi Imager, a finished OOBE, ...).
    bool user_has_password = false;
    // The stored hash still verifies against the factory default password that
    // pi-gen bakes into the image ("raspberry"), so an existing password does
    // NOT mean the user configured the device.
    bool factory_credentials = false;
    // Legacy piwiz/lightdm first-boot autologin is still armed.
    bool legacy_piwiz_active = false;
};

// A user-requested re-run always shows the wizard. The factory marker shows it
// only while the account is exactly in factory state: default username with
// either no password at all or the baked default password. A changed username
// or a user-chosen password both mean the device was provisioned (Raspberry Pi
// Imager), so first boot skips straight to the launcher.
bool should_run_wizard(const FirstBootState &state);

}  // namespace launch_wizard

#endif  // LAUNCH_WIZARD_FIRST_BOOT_POLICY_H
