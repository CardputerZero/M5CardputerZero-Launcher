#pragma once

#include <cstddef>
#include <list>
#include <string_view>
#include <string>

namespace cp0::settings {

using Arguments = std::list<std::string>;

enum class Command {
    Unknown,
    BacklightRead,
    BacklightMax,
    BacklightWrite,
    Log,
    TimeStr,
    GpioSet,
    GpioGet,
};

enum class PowerOutput {
    Unknown,
    Grove5V,
    Ext5V,
    Backlight,
};

Command command_from(const Arguments &arguments);
bool valid_arguments(const Arguments &arguments);
std::string argument_at(const Arguments &arguments, std::size_t index);
bool integer_argument(const Arguments &arguments,
                      std::size_t index,
                      int minimum,
                      int maximum,
                      int &value);
bool parse_integer_response(std::string_view text,
                            int minimum,
                            int maximum,
                            int &value);
bool parse_integer_file_line(std::string_view text,
                             int minimum,
                             int maximum,
                             int &value);
bool parse_switch_response(std::string_view text, int &value);
std::string gpiochip_path_from_debug_line(std::string_view line,
                                          std::string_view controller_name);

PowerOutput power_output_from_name(const std::string &name);
int switch_value(int value);
int physical_line_value(int logical_value, bool active_low);
int logical_line_value(int physical_value, bool active_low);

} // namespace cp0::settings
