#include "cp0_settings_policy.hpp"

#include <cassert>
#include <climits>

using cp0::settings::Arguments;
using cp0::settings::Command;
using cp0::settings::PowerOutput;

int main()
{
    assert(cp0::settings::command_from({}) == Command::Unknown);
    assert(cp0::settings::command_from({"BacklightRead"}) == Command::BacklightRead);
    assert(cp0::settings::command_from({"BacklightMax"}) == Command::BacklightMax);
    assert(cp0::settings::command_from({"BacklightWrite", "50"}) == Command::BacklightWrite);
    assert(cp0::settings::command_from({"Log"}) == Command::Log);
    assert(cp0::settings::command_from({"TimeStr"}) == Command::TimeStr);
    assert(cp0::settings::command_from({"GpioSet"}) == Command::GpioSet);
    assert(cp0::settings::command_from({"GpioGet"}) == Command::GpioGet);
    assert(cp0::settings::command_from({"backlightread"}) == Command::Unknown);
    assert(cp0::settings::valid_arguments({"BacklightRead"}));
    assert(!cp0::settings::valid_arguments({"BacklightRead", "junk"}));
    assert(cp0::settings::valid_arguments({"BacklightWrite", "0"}));
    assert(cp0::settings::valid_arguments({"BacklightWrite", "100"}));
    assert(!cp0::settings::valid_arguments({"BacklightWrite"}));
    assert(!cp0::settings::valid_arguments({"BacklightWrite", "12junk"}));
    assert(!cp0::settings::valid_arguments({"BacklightWrite", "-1"}));
    assert(cp0::settings::valid_arguments({"GpioSet", "GROVE5V", "1"}));
    assert(cp0::settings::valid_arguments({"GpioSet", "EXT5V", "0"}));
    assert(cp0::settings::valid_arguments({"GpioSet", "BACKLIGHT", "0"}));
    assert(!cp0::settings::valid_arguments({"GpioSet", "EXT5V", "-12"}));
    assert(!cp0::settings::valid_arguments({"GpioSet", "unknown", "1"}));
    assert(cp0::settings::valid_arguments({"GpioGet", "GROVE5V"}));
    assert(!cp0::settings::valid_arguments({"GpioGet", "unknown"}));
    assert(cp0::settings::valid_arguments({"Log", "topic", "message"}));
    assert(!cp0::settings::valid_arguments({"Log", "topic"}));

    const Arguments arguments{"GpioSet", "GROVE5V", "1"};
    assert(cp0::settings::argument_at(arguments, 0) == "GpioSet");
    assert(cp0::settings::argument_at(arguments, 1) == "GROVE5V");
    assert(cp0::settings::argument_at(arguments, 3).empty());
    int parsed = -1;
    assert(cp0::settings::integer_argument(arguments, 2, 0, 1, parsed) && parsed == 1);
    assert(!cp0::settings::integer_argument(arguments, 3, 0, 1, parsed));
    assert(!cp0::settings::integer_argument({"BacklightWrite", "invalid"}, 1, 0, 100, parsed));
    assert(cp0::settings::parse_integer_response("2147483647", 0, INT_MAX, parsed) &&
           parsed == INT_MAX);
    assert(!cp0::settings::parse_integer_response("12junk", 0, INT_MAX, parsed));
    assert(!cp0::settings::parse_integer_response(" 12", 0, INT_MAX, parsed));
    assert(!cp0::settings::parse_integer_response("-1", 0, INT_MAX, parsed));
    assert(cp0::settings::parse_integer_file_line("12\n", 0, 100, parsed) && parsed == 12);
    assert(cp0::settings::parse_integer_file_line("12\r\n", 0, 100, parsed) && parsed == 12);
    assert(cp0::settings::parse_integer_file_line("255\n", 0, INT_MAX, parsed) &&
           cp0::settings::switch_value(parsed) == 1);
    assert(!cp0::settings::parse_integer_file_line("12junk\n", 0, 100, parsed));
    assert(cp0::settings::parse_switch_response("0", parsed) && parsed == 0);
    assert(cp0::settings::parse_switch_response("1", parsed) && parsed == 1);
    assert(!cp0::settings::parse_switch_response("2", parsed));
    assert(cp0::settings::gpiochip_path_from_debug_line(
               "gpiochip2: GPIOs 0-15, parent: platform/m5ioe1, m5ioe1, can sleep:\n",
               "m5ioe1") == "/dev/gpiochip2");
    assert(cp0::settings::gpiochip_path_from_debug_line(
               "  gpiochip12 : GPIOs 0-15, m5ioe1:\n", "m5ioe1") ==
           "/dev/gpiochip12");
    assert(cp0::settings::gpiochip_path_from_debug_line(
               "gpiochip2: GPIOs 0-15, other-controller:\n", "m5ioe1").empty());
    assert(cp0::settings::gpiochip_path_from_debug_line(
               "not-a-chip: m5ioe1\n", "m5ioe1").empty());

    assert(cp0::settings::power_output_from_name("GROVE5V") == PowerOutput::Grove5V);
    assert(cp0::settings::power_output_from_name("extport_usb") == PowerOutput::Grove5V);
    assert(cp0::settings::power_output_from_name("EXT5V") == PowerOutput::Ext5V);
    assert(cp0::settings::power_output_from_name("extport_5vout") == PowerOutput::Ext5V);
    assert(cp0::settings::power_output_from_name("BACKLIGHT") == PowerOutput::Backlight);
    assert(cp0::settings::power_output_from_name("") == PowerOutput::Unknown);
    assert(cp0::settings::power_output_from_name("ext5v") == PowerOutput::Unknown);

    assert(cp0::settings::switch_value(0) == 0);
    assert(cp0::settings::switch_value(1) == 1);
    assert(cp0::settings::switch_value(-1) == 1);
    assert(cp0::settings::physical_line_value(0, false) == 0);
    assert(cp0::settings::physical_line_value(8, false) == 1);
    assert(cp0::settings::physical_line_value(0, true) == 1);
    assert(cp0::settings::physical_line_value(8, true) == 0);
    assert(cp0::settings::logical_line_value(0, true) == 1);
    assert(cp0::settings::logical_line_value(1, true) == 0);
    return 0;
}
