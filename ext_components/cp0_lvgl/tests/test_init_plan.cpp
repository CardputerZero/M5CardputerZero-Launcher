#include "cp0_init_plan.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {

std::vector<cp0_init_step_t> build(std::uint64_t features)
{
    std::array<cp0_init_step_t, CP0_INIT_STEP_SOUNDCARD + 1> storage{};
    const std::size_t count = cp0_build_init_plan(features, storage.data(), storage.size());
    assert(count <= storage.size());
    return {storage.begin(), storage.begin() + static_cast<std::ptrdiff_t>(count)};
}

std::size_t position(const std::vector<cp0_init_step_t> &plan, cp0_init_step_t step)
{
    const auto found = std::find(plan.begin(), plan.end(), step);
    assert(found != plan.end());
    return static_cast<std::size_t>(found - plan.begin());
}

void test_core_plan()
{
    const auto plan = build(0);
    assert((plan == std::vector<cp0_init_step_t>{
        CP0_INIT_STEP_ENV,
        CP0_INIT_STEP_EVENT,
        CP0_INIT_STEP_DISPLAY,
        CP0_INIT_STEP_INPUT,
    }));
}

void test_feature_order()
{
    const std::uint64_t all_features =
        (CP0_INIT_FEATURE_SOUNDCARD << 1) - 1;
    const auto plan = build(all_features);
    assert(plan.size() == static_cast<std::size_t>(CP0_INIT_STEP_SOUNDCARD + 1));
    assert(plan.front() == CP0_INIT_STEP_ENV);
    assert(plan.back() == CP0_INIT_STEP_SOUNDCARD);
    assert(position(plan, CP0_INIT_STEP_EVENT) < position(plan, CP0_INIT_STEP_FILESYSTEM));
    assert(position(plan, CP0_INIT_STEP_PTY) < position(plan, CP0_INIT_STEP_DISPLAY));
    assert(position(plan, CP0_INIT_STEP_DISPLAY) < position(plan, CP0_INIT_STEP_INPUT));
    assert(position(plan, CP0_INIT_STEP_INPUT) < position(plan, CP0_INIT_STEP_RPC));
    assert(position(plan, CP0_INIT_STEP_RPC) < position(plan, CP0_INIT_STEP_AUDIO));
    assert(position(plan, CP0_INIT_STEP_SCREENSHOT) < position(plan, CP0_INIT_STEP_LORA));
    assert(position(plan, CP0_INIT_STEP_BQ27220) < position(plan, CP0_INIT_STEP_SAVED_SETTINGS));
    assert(position(plan, CP0_INIT_STEP_BATTERY) < position(plan, CP0_INIT_STEP_CAMERA));
}

void test_selected_features_and_capacity()
{
    const auto plan = build(CP0_INIT_FEATURE_CONFIG | CP0_INIT_FEATURE_WIFI |
        CP0_INIT_FEATURE_SCREENSHOT);
    assert((plan == std::vector<cp0_init_step_t>{
        CP0_INIT_STEP_ENV,
        CP0_INIT_STEP_EVENT,
        CP0_INIT_STEP_CONFIG,
        CP0_INIT_STEP_DISPLAY,
        CP0_INIT_STEP_INPUT,
        CP0_INIT_STEP_SCREENSHOT,
        CP0_INIT_STEP_WIFI,
    }));

    cp0_init_step_t first_two[2]{};
    const std::size_t count = cp0_build_init_plan(CP0_INIT_FEATURE_AUDIO, first_two, 2);
    assert(count == 5);
    assert(first_two[0] == CP0_INIT_STEP_ENV && first_two[1] == CP0_INIT_STEP_EVENT);
    assert(cp0_build_init_plan(CP0_INIT_FEATURE_AUDIO, nullptr, 0) == 5);
}

} // namespace

int main()
{
    test_core_plan();
    test_feature_order();
    test_selected_features_and_capacity();
}
