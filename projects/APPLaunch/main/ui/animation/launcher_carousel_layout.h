/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl/lvgl.h"

#include <array>
#include <cstddef>

namespace launcher_carousel_layout {

struct Slot
{
    lv_coord_t x;
    lv_coord_t y;
    lv_coord_t width;
    lv_coord_t height;
    bool hidden;
};

inline constexpr size_t kPanelCount = 5;
inline constexpr size_t kTitleOffset = kPanelCount;
inline constexpr size_t kElementCount = kPanelCount * 2;
inline constexpr lv_coord_t kSideCardRadius = 17;
inline constexpr lv_coord_t kCenterCardRadius = 22;

inline constexpr std::array<Slot, kElementCount> kSlots = {{
    {-177, 4, 61, 61, true},
    {-99, -6, 80, 80, false},
    {0, -16, 100, 100, false},
    {99, -6, 80, 80, false},
    {177, 4, 61, 61, true},
    {-177, 50, 0, 0, true},
    {-99, 50, 0, 0, false},
    {0, 50, 0, 0, false},
    {99, 50, 0, 0, false},
    {177, 50, 0, 0, true},
}};

static_assert(kTitleOffset + kPanelCount == kElementCount);

} // namespace launcher_carousel_layout
