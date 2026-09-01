#pragma once

#include <string_view>

namespace setup_values {

enum class VolumeMetric : int {
    MinPercent = 0,
    MaxPercent = 100,
    StepPercent = 10,
    OptionCount = (MaxPercent - MinPercent) / StepPercent + 1,
};

constexpr int volume_metric(VolumeMetric metric)
{
    return static_cast<int>(metric);
}

struct CameraResolution {
    int width;
    int height;
};

int brightness_index(int value, int maximum);
int brightness_value(int index, int maximum);
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
