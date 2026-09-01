#include "../main/ui/model/launcher_media_model.hpp"
#include "../main/ui/model/launcher_media_osd_contract.hpp"

#include <cassert>
#include <climits>

int main()
{
    int parsed = 7;
    assert(LauncherMediaControlsModel::parse_int("0", parsed) && parsed == 0);
    assert(LauncherMediaControlsModel::parse_int("-42", parsed) && parsed == -42);
    assert(!LauncherMediaControlsModel::parse_int("", parsed));
    assert(!LauncherMediaControlsModel::parse_int("12px", parsed));
    assert(!LauncherMediaControlsModel::parse_int(" 12", parsed));
    assert(!LauncherMediaControlsModel::parse_int("999999999999999999999", parsed));
    assert(LauncherMediaControlsModel::parse_percent("100", parsed) && parsed == 100);
    assert(!LauncherMediaControlsModel::parse_percent("-1", parsed));
    assert(!LauncherMediaControlsModel::parse_percent("101", parsed));

    int container = 0;
    int icon = 0;
    int title = 0;
    int value = 0;
    int bar = 0;
    assert(launcher_media_osd_ui_ready(&container, &icon, &title, &value, &bar));
    assert(!launcher_media_osd_ui_ready(nullptr, &icon, &title, &value, &bar));
    assert(!launcher_media_osd_ui_ready(&container, nullptr, &title, &value, &bar));
    assert(!launcher_media_osd_ui_ready(&container, &icon, &title, &value, nullptr));
    int timer = 0;
    int stale_timer = 0;
    assert(launcher_media_osd_timer_is_current(&timer, &timer));
    assert(!launcher_media_osd_timer_is_current(&stale_timer, &timer));
    assert(!launcher_media_osd_timer_is_current(nullptr, &timer));

    LauncherMediaControlsModel controls;
    assert(!controls.has_volume());
    assert(controls.volume_or(50) == 50);
    controls.set_volume(110);
    assert(controls.has_volume());
    assert(controls.volume_or(0) == 100);
    controls.set_volume(-10);
    assert(controls.volume_or(50) == 0);

    assert(LauncherMediaControlsModel::percent_from_raw(25, 50) == 50);
    assert(LauncherMediaControlsModel::percent_from_raw(200, 50) == 100);
    assert(LauncherMediaControlsModel::percent_from_raw(INT_MAX, INT_MAX) == 100);
    assert(LauncherMediaControlsModel::percent_from_raw(INT_MIN, INT_MAX) == 0);
    assert(LauncherMediaControlsModel::raw_from_percent(1, 40) == 1);
    assert(LauncherMediaControlsModel::raw_from_percent(50, 255) == 127);
    assert(LauncherMediaControlsModel::raw_from_percent(100, INT_MAX) == INT_MAX);
    assert(controls.toggle_mute());
    assert(controls.muted());
    assert(!controls.toggle_mute());
    assert(!controls.muted());
    controls.set_mute(true);
    assert(!controls.toggle_mute());

    LauncherMediaOsdModel osd;
    assert(!osd.state().visible);
    osd.show_level("Volume", "V", 120);
    assert(osd.state().visible);
    assert(osd.state().mode == LauncherMediaOsdMode::LEVEL);
    assert(osd.state().title == "Volume");
    assert(osd.state().percent == 100);
    osd.hide();
    assert(!osd.state().visible);
    osd.show_mute(true, "M", "V");
    assert(osd.state().mode == LauncherMediaOsdMode::MUTE);
    assert(osd.state().title == "Muted");
    assert(osd.state().icon == "M");
    osd.show_mute(false, "M", "V");
    assert(osd.state().visible);
    assert(osd.state().title == "Sound On");
    assert(osd.state().icon == "V");
    osd.hide();
    osd.hide();
    assert(!osd.state().visible);
    osd.show_level("Brightness", "B", 25);
    assert(osd.state().visible);
    assert(osd.state().percent == 25);
    return 0;
}
