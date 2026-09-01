/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace brightness_policy {

enum class BrightnessMetric : int {
    MinPercent = 0,
    MaxPercent = 100,
    StepPercent = 25,
    OptionCount = (MaxPercent - MinPercent) / StepPercent + 1,
};

constexpr int brightness_metric(BrightnessMetric metric)
{
    return static_cast<int>(metric);
}

constexpr int percent_for_index(int index)
{
    constexpr int option_count = brightness_metric(BrightnessMetric::OptionCount);
    constexpr int max_percent = brightness_metric(BrightnessMetric::MaxPercent);
    constexpr int step_percent = brightness_metric(BrightnessMetric::StepPercent);
    if (index < 0) index = 0;
    if (index >= option_count) index = option_count - 1;
    return max_percent - index * step_percent;
}

} // namespace brightness_policy
