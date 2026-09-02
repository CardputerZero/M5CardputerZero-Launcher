/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launcher_media_controls.h"

#include "hal_lvgl_bsp.h"
#include "model/brightness_operation.hpp"
#include "model/launcher_media_model.hpp"
#include "model/setup_value_policy.hpp"

#include <string>

namespace {

LauncherMediaControlsModel model;

int read_config_int(const char *key, int fallback)
{
    int value = fallback;
    cp0_signal_config_api({"GetInt", key, std::to_string(fallback)},
                          [&](int code, std::string data) {
                              int parsed = 0;
                              if (code == 0 && LauncherMediaControlsModel::parse_int(data, parsed))
                                  value = parsed;
                          });
    return value;
}

bool call_config(const std::list<std::string> &arguments)
{
    bool succeeded = false;
    cp0_signal_config_api(arguments, [&](int code, std::string) { succeeded = code == 0; });
    return succeeded;
}

bool write_config_int(const char *key, int value, int fallback)
{
    const int previous = read_config_int(key, fallback);
    if (!call_config({"SetInt", key, std::to_string(value)})) return false;
    if (call_config({"Save"})) return true;

    if (call_config({"SetInt", key, std::to_string(previous)})) call_config({"Save"});
    return false;
}

int read_volume()
{
    int volume = -1;
    cp0_signal_audio_api({"VolumeRead"}, [&](int code, std::string data) {
        int parsed = 0;
        if (code == 0 && LauncherMediaControlsModel::parse_percent(data, parsed)) volume = parsed;
    });
    if (volume < 0)
        volume = read_config_int("volume", model.volume_or(50));
    return LauncherMediaControlsModel::clamp_percent(volume);
}

bool read_mute()
{
    bool muted = model.muted();
    cp0_signal_audio_api({"MuteRead"}, [&](int code, std::string data) {
        int parsed = 0;
        if (code == 0 && LauncherMediaControlsModel::parse_int(data, parsed) &&
            (parsed == 0 || parsed == 1)) {
            muted = parsed != 0;
            model.set_mute(muted);
        }
    });
    return muted;
}

int write_volume(int previous, int percent)
{
    percent = LauncherMediaControlsModel::clamp_percent(percent);
    int written = -1;
    cp0_signal_audio_api({"VolumeWrite", std::to_string(percent)}, [&](int code, std::string data) {
        int parsed = 0;
        if (code == 0 && LauncherMediaControlsModel::parse_percent(data, parsed)) written = parsed;
    });
    if (written < 0) return previous;
    if (!write_config_int("volume", written, previous)) {
        cp0_signal_audio_api({"VolumeWrite", std::to_string(previous)}, nullptr);
        return previous;
    }
    model.set_volume(written);
    return written;
}

int backlight_max()
{
    int maximum = 100;
    cp0_signal_settings_api({"BacklightMax"}, [&](int code, std::string data) {
        int parsed = 0;
        if (code == 0 && LauncherMediaControlsModel::parse_int(data, parsed) && parsed > 0)
            maximum = parsed;
    });
    return maximum > 0 ? maximum : 100;
}

int read_brightness()
{
    int raw = -1;
    const int maximum = backlight_max();
    cp0_signal_settings_api({"BacklightRead"}, [&](int code, std::string data) {
        int parsed = 0;
        if (code == 0 && LauncherMediaControlsModel::parse_int(data, parsed) && parsed >= 0)
            raw = parsed;
    });
    if (raw < 0) {
        const int fallback_index = setup_values::brightness_step_index(
            model.brightness_or(setup_values::kBrightnessMaxPercent));
        const int fallback = setup_values::brightness_step_value(fallback_index, maximum);
        raw = read_config_int(setup_values::kBrightnessConfigKey, fallback);
    }
    const int percent = setup_values::brightness_step_percent_from_raw(raw, maximum);
    model.set_brightness(percent);
    return percent;
}

int write_brightness(int previous_percent, int percent)
{
    const int target_index = setup_values::brightness_step_index(percent);
    const int maximum = backlight_max();
    const int raw = setup_values::brightness_step_value(target_index, maximum);

    int written = -1;
    cp0_signal_settings_api({"BacklightWrite", std::to_string(raw)}, [&](int code, std::string data) {
        int parsed = 0;
        if (code == 0 && LauncherMediaControlsModel::parse_int(data, parsed) && parsed >= 0)
            written = parsed;
    });
    if (written < 0) return previous_percent;
    const int previous_raw = setup_values::brightness_step_value(
        setup_values::brightness_step_index(previous_percent), maximum);
    if (!write_config_int(setup_values::kBrightnessConfigKey, written, previous_raw)) {
        cp0_signal_settings_api({"BacklightWrite", std::to_string(previous_raw)}, nullptr);
        return previous_percent;
    }
    const int applied_percent = setup_values::brightness_step_percent_from_raw(written, maximum);
    model.set_brightness(applied_percent);
    return applied_percent;
}

} // namespace

namespace launcher_media_controls {

int adjust_volume(int delta_percent)
{
    if (read_mute())
        (void)toggle_mute();
    const int current = read_volume();
    const int base = setup_values::round_volume_percent(current);
    const int delta = delta_percent > 0 ? VOLUME_STEP_PERCENT
                                       : delta_percent < 0 ? -VOLUME_STEP_PERCENT : 0;
    return write_volume(current, base + delta);
}

int adjust_brightness(int delta_percent)
{
    std::unique_lock<std::mutex> operation_lock(
        brightness_control::operation_mutex(), std::try_to_lock);
    if (!operation_lock.owns_lock()) {
        return setup_values::brightness_step_percent(
            setup_values::brightness_step_index(
                model.brightness_or(setup_values::kBrightnessMaxPercent)));
    }

    const int current = read_brightness();
    const int target = setup_values::brightness_step_percent_after(current, delta_percent);
    return write_brightness(current, target);
}

bool toggle_mute()
{
    const bool previous = model.muted();
    bool muted = previous;
    cp0_signal_audio_api({"MuteToggle"}, [&](int code, std::string data) {
        int parsed = 0;
        if (code == 0 && LauncherMediaControlsModel::parse_int(data, parsed) &&
            (parsed == 0 || parsed == 1)) {
            muted = parsed != 0;
            model.set_mute(muted);
        }
    });
    return muted;
}

} // namespace launcher_media_controls
