/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "model/setup_value_policy.hpp"

namespace launcher_media_controls {

constexpr int VOLUME_STEP_PERCENT =
    setup_values::volume_metric(setup_values::VolumeMetric::StepPercent);

int adjust_volume(int delta_percent);
int adjust_brightness(int delta_percent);
bool toggle_mute();

} // namespace launcher_media_controls
