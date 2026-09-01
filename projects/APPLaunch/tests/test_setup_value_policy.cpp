#include "../main/ui/model/setup_value_policy.hpp"

#include <cassert>
#include <climits>

int main()
{
    assert(setup_values::brightness_index(100, 100) == 0);
    assert(setup_values::brightness_index(86, 100) == 1);
    assert(setup_values::brightness_index(61, 100) == 2);
    assert(setup_values::brightness_index(36, 100) == 3);
    assert(setup_values::brightness_index(20, 0) == 0);
    assert(setup_values::brightness_value(0, 200) == 200);
    assert(setup_values::brightness_value(2, 200) == 100);
    assert(setup_values::brightness_value(99, 3) == 1);
    assert(setup_values::brightness_value(-1, 0) == 1);
    assert(setup_values::brightness_index(INT_MAX, INT_MAX) == 0);
    assert(setup_values::brightness_value(2, INT_MAX) == INT_MAX / 2);

    assert(setup_values::kBrightnessStepCount == 10);
    assert(setup_values::brightness_step_percent(0) == 100);
    assert(setup_values::brightness_step_percent(9) == 10);
    assert(setup_values::brightness_step_index(100) == 0);
    assert(setup_values::brightness_step_index(95) == 0);
    assert(setup_values::brightness_step_index(94) == 1);
    assert(setup_values::brightness_step_index(0) == 9);
    assert(setup_values::brightness_step_value(9, 100) == 10);
    assert(setup_values::brightness_step_percent_from_raw(64, 255) == 30);
    assert(setup_values::brightness_step_percent_after(100, 1) == 100);
    assert(setup_values::brightness_step_percent_after(100, -1) == 90);
    assert(setup_values::brightness_step_percent_after(10, -1) == 10);

    int parsed = -1;
    assert(setup_values::parse_nonnegative_int("0", parsed) && parsed == 0);
    assert(setup_values::parse_nonnegative_int("2147483647", parsed) && parsed == INT_MAX);
    assert(!setup_values::parse_nonnegative_int("", parsed));
    assert(!setup_values::parse_nonnegative_int("12junk", parsed));
    assert(!setup_values::parse_nonnegative_int(" 12", parsed));
    assert(!setup_values::parse_nonnegative_int("+12", parsed));
    assert(!setup_values::parse_nonnegative_int("-1", parsed));
    assert(!setup_values::parse_nonnegative_int("2147483648", parsed));

    assert(setup_values::dark_time_index(0) == 0);
    assert(setup_values::dark_time_index(300) == 4);
    assert(setup_values::dark_time_index(17) == 2);
    assert(setup_values::dark_time_seconds(-1) == 0);
    assert(setup_values::dark_time_seconds(99) == 300);

    assert(setup_values::round_volume_percent(-1) == 0);
    assert(setup_values::round_volume_percent(4) == 0);
    assert(setup_values::round_volume_percent(5) == 10);
    assert(setup_values::round_volume_percent(63) == 60);
    assert(setup_values::round_volume_percent(65) == 70);
    assert(setup_values::round_volume_percent(71) == 70);
    assert(setup_values::round_volume_percent(82) == 80);
    assert(setup_values::round_volume_percent(96) == 100);
    assert(setup_values::round_volume_percent(101) == 100);
    assert(setup_values::volume_index(100) == 0);
    assert(setup_values::volume_index(96) == 0);
    assert(setup_values::volume_index(82) == 2);
    assert(setup_values::volume_index(63) == 4);
    assert(setup_values::volume_index(5) == 9);
    assert(setup_values::volume_index(4) == 10);
    assert(setup_values::volume_percent(-1) == 100);
    assert(setup_values::volume_percent(99) == 0);
    assert(setup_values::volume_percent(4) == 60);
    assert(setup_values::volume_value_valid(0));
    assert(setup_values::volume_value_valid(100));
    assert(!setup_values::volume_value_valid(-1));
    assert(!setup_values::volume_value_valid(101));

    assert(setup_values::camera_resolution_index(640, 480) == 1);
    assert(setup_values::camera_resolution_index(1280, 720) == 0);
    assert(setup_values::camera_resolution_index(640, 720) == 0);
    assert(setup_values::camera_resolution_supported(1280, 720));
    assert(setup_values::camera_resolution_supported(640, 480));
    assert(!setup_values::camera_resolution_supported(640, 720));
    assert(setup_values::camera_available_from_status(true, 0));
    assert(setup_values::camera_available_from_status(true, 1));
    assert(!setup_values::camera_available_from_status(true, -10));
    assert(!setup_values::camera_available_from_status(false, 0));
    const auto hd = setup_values::camera_resolution(-1);
    assert(hd.width == 1280 && hd.height == 720);
    const auto vga = setup_values::camera_resolution(99);
    assert(vga.width == 640 && vga.height == 480);
}
