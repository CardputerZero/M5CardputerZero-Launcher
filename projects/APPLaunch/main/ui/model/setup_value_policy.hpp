#pragma once

#include <string_view>

namespace setup_values {

inline constexpr char kBrightnessConfigKey[] = "brightness";
inline constexpr int kBrightnessMaxPercent = 100;
inline constexpr int kBrightnessMinPercent = 10;
inline constexpr int kBrightnessStepPercent = 10;
inline constexpr int kBrightnessStepCount =
    (kBrightnessMaxPercent - kBrightnessMinPercent) / kBrightnessStepPercent + 1;

struct CameraResolution {
    int width;
    int height;
};

int brightness_index(int value, int maximum);
int brightness_value(int index, int maximum);
int brightness_step_percent(int index);
int brightness_step_index(int percent);
int brightness_step_value(int index, int maximum);
int brightness_step_index_from_raw(int value, int maximum);
int brightness_step_percent_from_raw(int value, int maximum);
int brightness_step_percent_after(int current_percent, int direction);
bool parse_nonnegative_int(std::string_view text, int &value);

int dark_time_index(int seconds);
int dark_time_seconds(int index);

int round_volume_percent(int percent);
int volume_index(int percent);
int volume_percent(int index);
bool volume_value_valid(int percent);

int camera_resolution_index(int width, int height);
CameraResolution camera_resolution(int index);
bool camera_resolution_supported(int width, int height);
bool camera_available_from_status(bool callback_received, int status_code);

} // namespace setup_values
