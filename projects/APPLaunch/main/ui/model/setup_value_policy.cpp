#include "setup_value_policy.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>

namespace setup_values {
namespace {

constexpr int kBrightnessPercentages[] = {100, 75, 50, 25};
constexpr int kDarkTimes[] = {0, 10, 30, 60, 300};
constexpr CameraResolution kCameraResolutions[] = {{1280, 720}, {640, 480}};

template <typename T, int N>
constexpr int array_size(const T (&)[N])
{
    return N;
}

int clamp_index(int index, int count)
{
    return std::max(0, std::min(index, count - 1));
}

} // namespace

int brightness_index(int value, int maximum)
{
    const int percent = maximum > 0
                            ? static_cast<int>(static_cast<std::int64_t>(value) * 100 / maximum)
                            : 100;
    if (percent >= 87) return 0;
    if (percent >= 62) return 1;
    if (percent >= 37) return 2;
    return 3;
}

int brightness_value(int index, int maximum)
{
    const int safe_maximum = std::max(1, maximum);
    const int percentage = kBrightnessPercentages[
        clamp_index(index, array_size(kBrightnessPercentages))];
    return std::max(1, static_cast<int>(
                           static_cast<std::int64_t>(safe_maximum) * percentage / 100));
}

int brightness_step_percent(int index)
{
    const int safe_index = clamp_index(index, kBrightnessStepCount);
    return kBrightnessMaxPercent - safe_index * kBrightnessStepPercent;
}

int brightness_step_index(int percent)
{
    const int normalized = std::clamp(percent, kBrightnessMinPercent,
                                      kBrightnessMaxPercent);
    const int offset = kBrightnessMaxPercent - normalized;
    const int rounded_offset = offset + kBrightnessStepPercent / 2 - 1;
    return clamp_index(rounded_offset / kBrightnessStepPercent,
                       kBrightnessStepCount);
}

int brightness_step_value(int index, int maximum)
{
    const int safe_maximum = std::max(1, maximum);
    const int percentage = brightness_step_percent(index);
    return std::max(1, static_cast<int>(
                           static_cast<std::int64_t>(safe_maximum) * percentage /
                           kBrightnessMaxPercent));
}

int brightness_step_index_from_raw(int value, int maximum)
{
    if (maximum <= 0) return 0;
    const std::int64_t bounded = std::clamp<std::int64_t>(value, 0, maximum);
    const int percent = static_cast<int>(bounded * kBrightnessMaxPercent / maximum);
    return brightness_step_index(percent);
}

int brightness_step_percent_from_raw(int value, int maximum)
{
    return brightness_step_percent(brightness_step_index_from_raw(value, maximum));
}

int brightness_step_percent_after(int current_percent, int direction)
{
    const int current_index = brightness_step_index(current_percent);
    const int index_delta = direction > 0 ? -1 : direction < 0 ? 1 : 0;
    return brightness_step_percent(current_index + index_delta);
}

bool parse_nonnegative_int(std::string_view text, int &value)
{
    if (text.empty()) return false;
    int parsed = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < 0) return false;
    value = parsed;
    return true;
}

int dark_time_index(int seconds)
{
    for (int i = 0; i < array_size(kDarkTimes); ++i) {
        if (kDarkTimes[i] == seconds) return i;
    }
    return 2;
}

int dark_time_seconds(int index)
{
    return kDarkTimes[clamp_index(index, array_size(kDarkTimes))];
}

int round_volume_percent(int percent)
{
    constexpr int min_percent = volume_metric(VolumeMetric::MinPercent);
    constexpr int max_percent = volume_metric(VolumeMetric::MaxPercent);
    constexpr int step_percent = volume_metric(VolumeMetric::StepPercent);
    const int clamped = std::clamp(percent, min_percent, max_percent);
    const int rounded = ((clamped - min_percent + step_percent / 2) /
                         step_percent) * step_percent + min_percent;
    return std::min(max_percent, rounded);
}

int volume_index(int percent)
{
    return (100 - round_volume_percent(percent)) / 10;
}

int volume_percent(int index)
{
    constexpr int max_percent = volume_metric(VolumeMetric::MaxPercent);
    constexpr int step_percent = volume_metric(VolumeMetric::StepPercent);
    const int clamped = clamp_index(index, volume_metric(VolumeMetric::OptionCount));
    return max_percent - clamped * step_percent;
}

bool volume_value_valid(int percent)
{
    return percent >= volume_metric(VolumeMetric::MinPercent) &&
           percent <= volume_metric(VolumeMetric::MaxPercent);
}

int camera_resolution_index(int width, int height)
{
    return width == 640 && height == 480 ? 1 : 0;
}

CameraResolution camera_resolution(int index)
{
    return kCameraResolutions[clamp_index(index, array_size(kCameraResolutions))];
}

bool camera_resolution_supported(int width, int height)
{
    for (const auto &resolution : kCameraResolutions) {
        if (resolution.width == width && resolution.height == height) return true;
    }
    return false;
}

bool camera_available_from_status(bool callback_received, int status_code)
{
    return callback_received && status_code >= 0;
}

} // namespace setup_values
