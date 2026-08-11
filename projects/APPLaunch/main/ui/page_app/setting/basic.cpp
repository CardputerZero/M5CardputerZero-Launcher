#define APP_PAGE_IMPLEMENTATION_UNIT
#include "../ui_app_setup.hpp"
#include "../../model/boot_action_policy.hpp"
#include "../../model/setup_value_policy.hpp"
#include "setup_page_access.hpp"

#include <algorithm>

namespace setting {
namespace {

void execute_boot_action(boot_actions::Action action)
{
    for (const auto operation : boot_actions::operation_plan(action)) {
        bool succeeded = true;
        switch (operation) {
        case boot_actions::Operation::Reboot:
            cp0_signal_process_api({"Reboot"}, nullptr);
            break;
        case boot_actions::Operation::Shutdown:
            cp0_signal_process_api({"Shutdown"}, nullptr);
            break;
        case boot_actions::Operation::RemoveLauncherSettings:
            succeeded = false;
            cp0_signal_filesystem_api({"Remove", launcher_platform::path("launcher_settings")},
                [&](int code, std::string) { succeeded = code == 0; });
            break;
        case boot_actions::Operation::TouchOobeMarker:
            succeeded = false;
            cp0_signal_filesystem_api({"Touch", launcher_platform::path("oobe_marker")},
                [&](int code, std::string) { succeeded = code == 0; });
            break;
        }
        if (!boot_actions::may_continue(operation, succeeded)) return;
    }
}

int read_config_int_strict(const char *key, int fallback)
{
    int value = fallback;
    cp0_signal_config_api({"GetInt", key, std::to_string(fallback)},
        [&](int code, std::string data) {
            int parsed = 0;
            if (code == 0 && setup_values::parse_nonnegative_int(data, parsed)) value = parsed;
        });
    return value;
}

bool write_config_int_checked(const char *key, int value)
{
    bool succeeded = false;
    cp0_signal_config_api({"SetInt", key, std::to_string(value)},
                          [&](int code, std::string) { succeeded = code == 0; });
    return succeeded;
}

bool save_config_checked()
{
    bool succeeded = false;
    cp0_signal_config_api({"Save"},
                          [&](int code, std::string) { succeeded = code == 0; });
    return succeeded;
}

void restore_camera_resolution(int width, int height)
{
    write_config_int_checked("camera.resolution.width", width);
    write_config_int_checked("camera.resolution.height", height);
    save_config_checked();
}

} // namespace

void Launcher::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    UISetupPage *page = &p;
    MenuItem m;
    m.label = "Launcher";
    std::size_t app_count = 0;
    const AppDescriptor *apps = launcher_app_registry_entries(&app_count);
    for (std::size_t i = 0; i < app_count; ++i) {
        const AppDescriptor &desc = apps[i];
        if (!desc.configurable)
            continue;
        bool enabled = launcher_app_registry_is_enabled(desc);
        m.sub_items.push_back({desc.label, true, enabled,
            [page, key = std::string(desc.config_key)]() { Launcher::save_app_toggle(*page, key); }});
    }
    menu.push_back(m);
}

void Launcher::save_app_toggle(UISetupPage &page, const std::string &config_key)
{
    SetupPageAccess access(page);
    MenuItem *launcher_menu = access.find_menu("Launcher");
    if (!launcher_menu)
        return;

    std::size_t app_count = 0;
    const AppDescriptor *apps = launcher_app_registry_entries(&app_count);
    std::size_t visible_idx = 0;
    for (std::size_t i = 0; i < app_count; ++i) {
        const AppDescriptor &desc = apps[i];
        if (!desc.configurable)
            continue;
        if (config_key == desc.config_key) {
            if (visible_idx >= launcher_menu->sub_items.size())
                return;
            bool enabled = launcher_menu->sub_items[visible_idx].toggle_state;
            if (!launcher_app_registry_set_enabled(desc, enabled)) {
                launcher_menu->sub_items[visible_idx].toggle_state =
                    launcher_app_registry_is_enabled(desc);
                return;
            }
            launcher_app_registry_notify_changed();
            return;
        }
        ++visible_idx;
    }
}

void Boot::factory_reset()
{
    execute_boot_action(boot_actions::Action::FactoryReset);
}

void Boot::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    const auto make_item = [&p](boot_actions::Action action) {
        const auto &view = boot_actions::presentation(action);
        return SubItem{std::string(view.label), false, false, [&p, action]() {
            const auto &selected = boot_actions::presentation(action);
            SetupPageAccess(p).confirm(selected.confirmation_title.data(),
                [action]() { execute_boot_action(action); });
        }};
    };
    MenuItem m;
    m.label = "Boot";
    m.sub_items = {
        make_item(boot_actions::Action::Reboot),
        make_item(boot_actions::Action::Shutdown),
    };
    menu.push_back(m);
}

void Boot::rearm_oobe_and_reboot()
{
    execute_boot_action(boot_actions::Action::RearmOobe);
}

void Screen::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    UISetupPage *page = &p;
    Screen *controller = this;
    MenuItem m;
    m.label = "Screen";
    m.sub_items = {
        {"Brightness", false, false,
            [controller, page]() { controller->enter_brightness_adjust(*page); }},
        {"DarkTime", false, false,
            [controller, page]() { controller->enter_darktime_adjust(*page); }},
    };
    menu.push_back(m);
}

int Screen::backlight_read()
{
    int value = -1;
    cp0_signal_settings_api({"BacklightRead"}, [&](int code, std::string data) {
        int parsed = -1;
        if (code == 0 && setup_values::parse_nonnegative_int(data, parsed)) value = parsed;
    });
    return value;
}

int Screen::backlight_max()
{
    int value = 100;
    cp0_signal_settings_api({"BacklightMax"}, [&](int code, std::string data) {
        int parsed = 0;
        if (code == 0 && setup_values::parse_nonnegative_int(data, parsed) && parsed > 0)
            value = parsed;
    });
    return value;
}

void Screen::enter_brightness_adjust(UISetupPage &page)
{
    SetupPageAccess access(page);
    const int maximum = backlight_max();
    bright_val_ = backlight_read();
    if (bright_val_ < 0 || bright_val_ > maximum)
        bright_val_ = access.config_get_int("brightness", maximum);
    bright_val_ = std::clamp(bright_val_, 1, maximum);
    access.enter_value("Brightness", {"100%", "75%", "50%", "25%"},
                       setup_values::brightness_index(bright_val_, maximum));
}

void Screen::apply_value(UISetupPage &page)
{
    SetupPageAccess access(page);
    if (access.value_title() == "DarkTime") {
        const int previous = access.config_get_int("dark_time", 30);
        const int desired = setup_values::dark_time_seconds(access.value_selection());
        if (!access.config_set_int("dark_time", desired) || !access.config_save()) {
            access.config_set_int("dark_time", previous);
            access.config_save();
        }
        return;
    }
    if (access.value_title() != "Brightness") return;

    const int maximum = backlight_max();
    const int new_val = setup_values::brightness_value(access.value_selection(), maximum);
    bool write_succeeded = false;
    int applied_value = new_val;
    cp0_signal_settings_api({"BacklightWrite", std::to_string(new_val)},
        [&](int code, std::string data) {
            if (code != 0) return;
            write_succeeded = true;
            int parsed = 0;
            if (setup_values::parse_nonnegative_int(data, parsed)) applied_value = parsed;
        });
    if (!write_succeeded) return;
    const int previous_brightness = bright_val_;
    const int applied_brightness = std::clamp(applied_value, 1, maximum);
    const int previous_config = std::clamp(
        access.config_get_int("brightness", previous_brightness), 1, maximum);
    if (!access.config_set_int("brightness", applied_brightness) || !access.config_save()) {
        access.config_set_int("brightness", previous_config);
        access.config_save();
        cp0_signal_settings_api(
            {"BacklightWrite", std::to_string(previous_brightness)}, nullptr);
        return;
    }
    bright_val_ = applied_brightness;
}

void Screen::enter_darktime_adjust(UISetupPage &page)
{
    SetupPageAccess access(page);
    access.enter_value("DarkTime", {"Never", "10S", "30S", "60S", "300S"},
        setup_values::dark_time_index(access.config_get_int("dark_time", 30)));
}

void Speaker::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    UISetupPage *page = &p;
    Speaker *controller = this;
    MenuItem m;
    m.label = "Speaker";
    m.sub_items = {{"Volume", false, false,
        [controller, page]() { controller->enter_volume_adjust(*page); }}};
    menu.push_back(m);
}

void Speaker::enter_volume_adjust(UISetupPage &page)
{
    SetupPageAccess access(page);
    vol_val_ = access.audio_volume_read();
    if (!setup_values::volume_value_valid(vol_val_))
        vol_val_ = read_config_int_strict("volume", 100);
    vol_val_ = std::clamp(vol_val_, 0, 100);
    preview_active_ = true;
    access.enter_value("Volume", {"100%", "90%", "80%", "70%", "60%", "50%",
                                  "40%", "30%", "20%", "10%", "0%"},
        setup_values::volume_index(vol_val_));
}

bool Speaker::preview_value(UISetupPage &page)
{
    SetupPageAccess access(page);
    if (!preview_active_ || access.value_title() != "Volume") return false;
    const int requested = setup_values::volume_percent(access.value_selection());
    return access.audio_volume_write(requested) == requested;
}

void Speaker::cancel_preview(UISetupPage &page)
{
    if (!preview_active_) return;
    SetupPageAccess(page).audio_volume_write(vol_val_);
    preview_active_ = false;
}

bool Speaker::apply_value(UISetupPage &page)
{
    SetupPageAccess access(page);
    if (access.value_title() != "Volume") return false;
    const int requested = setup_values::volume_percent(access.value_selection());
    const int previous_config = std::clamp(
        read_config_int_strict("volume", vol_val_), 0, 100);
    const int applied = access.audio_volume_write(requested);
    if (!setup_values::volume_value_valid(applied)) return false;
    if (!write_config_int_checked("volume", applied) || !save_config_checked()) {
        write_config_int_checked("volume", previous_config);
        save_config_checked();
        access.audio_volume_write(vol_val_);
        preview_active_ = false;
        return false;
    }
    vol_val_ = applied;
    preview_active_ = false;
    return true;
}

void Camera::append(UISetupPage &p, std::vector<MenuItem> &menu)
{
    bool status_received = false;
    int status_code = -1;
    cp0_signal_camera_api({"Status"}, [&](int code, std::string) {
        status_received = true;
        status_code = code;
    });
    if (!setup_values::camera_available_from_status(status_received, status_code)) return;

    UISetupPage *page = &p;
    Camera *controller = this;
    MenuItem m;
    m.label = "Camera";
    m.sub_items = {{"Resolution", false, false,
        [controller, page]() { controller->enter_resolution(*page); }}};
    menu.push_back(m);
}

void Camera::enter_resolution(UISetupPage &page)
{
    SetupPageAccess access(page);
    int width = read_config_int_strict("camera.resolution.width", 1280);
    int height = read_config_int_strict("camera.resolution.height", 720);
    if (!setup_values::camera_resolution_supported(width, height)) {
        width = 1280;
        height = 720;
    }
    access.enter_value("Resolution", {"1280x720", "640x480"},
        setup_values::camera_resolution_index(width, height));
}

void Camera::apply_value(UISetupPage &page)
{
    SetupPageAccess access(page);
    if (access.value_title() != "Resolution") return;
    const auto resolution = setup_values::camera_resolution(access.value_selection());
    const int previous_width = read_config_int_strict("camera.resolution.width", 1280);
    const int previous_height = read_config_int_strict("camera.resolution.height", 720);
    if (!write_config_int_checked("camera.resolution.width", resolution.width)) return;
    if (!write_config_int_checked("camera.resolution.height", resolution.height) ||
        !save_config_checked()) {
        restore_camera_resolution(previous_width, previous_height);
    }
}

} // namespace setting
