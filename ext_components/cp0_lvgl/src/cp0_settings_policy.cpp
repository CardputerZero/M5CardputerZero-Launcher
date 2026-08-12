#include "cp0_settings_policy.hpp"

#include <charconv>
#include <cctype>
#include <limits>

namespace cp0::settings {

Command command_from(const Arguments &arguments)
{
    if (arguments.empty())
        return Command::Unknown;

    const std::string &command = arguments.front();
    if (command == "BacklightRead")
        return Command::BacklightRead;
    if (command == "BacklightMax")
        return Command::BacklightMax;
    if (command == "BacklightWrite")
        return Command::BacklightWrite;
    if (command == "Log")
        return Command::Log;
    if (command == "TimeStr")
        return Command::TimeStr;
    if (command == "GpioSet")
        return Command::GpioSet;
    if (command == "GpioGet")
        return Command::GpioGet;
    return Command::Unknown;
}

bool valid_arguments(const Arguments &arguments)
{
    const Command command = command_from(arguments);
    if (command == Command::Unknown) return false;
    if (command == Command::BacklightRead || command == Command::BacklightMax ||
        command == Command::TimeStr)
        return arguments.size() == 1;
    if (command == Command::Log)
        return arguments.size() == 3;
    if (command == Command::GpioGet)
        return arguments.size() == 2 &&
               power_output_from_name(argument_at(arguments, 1)) != PowerOutput::Unknown;

    if (arguments.size() != (command == Command::BacklightWrite ? 2u : 3u))
        return false;
    int parsed = 0;
    if (!integer_argument(arguments, command == Command::BacklightWrite ? 1 : 2,
                          0, std::numeric_limits<int>::max(), parsed))
        return false;
    if (command == Command::GpioSet)
        return power_output_from_name(argument_at(arguments, 1)) != PowerOutput::Unknown &&
               (parsed == 0 || parsed == 1);
    return parsed >= 0;
}

std::string argument_at(const Arguments &arguments, std::size_t index)
{
    auto iterator = arguments.begin();
    for (std::size_t current = 0; current < index && iterator != arguments.end(); ++current)
        ++iterator;
    return iterator == arguments.end() ? std::string() : *iterator;
}

bool parse_integer_response(std::string_view text,
                            int minimum,
                            int maximum,
                            int &value)
{
    if (text.empty()) return false;
    int parsed = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < minimum || parsed > maximum)
        return false;
    value = parsed;
    return true;
}

bool integer_argument(const Arguments &arguments,
                      std::size_t index,
                      int minimum,
                      int maximum,
                      int &value)
{
    return parse_integer_response(argument_at(arguments, index), minimum, maximum, value);
}

bool parse_integer_file_line(std::string_view text,
                             int minimum,
                             int maximum,
                             int &value)
{
    if (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
    return parse_integer_response(text, minimum, maximum, value);
}

bool parse_switch_response(std::string_view text, int &value)
{
    return parse_integer_response(text, 0, 1, value);
}

std::string gpiochip_path_from_debug_line(std::string_view line,
                                          std::string_view controller_name)
{
    if (controller_name.empty() || line.find(controller_name) == std::string_view::npos)
        return {};
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) return {};
    std::string_view chip = line.substr(0, colon);
    while (!chip.empty() && std::isspace(static_cast<unsigned char>(chip.front())))
        chip.remove_prefix(1);
    while (!chip.empty() && std::isspace(static_cast<unsigned char>(chip.back())))
        chip.remove_suffix(1);
    constexpr std::string_view prefix = "gpiochip";
    if (chip.size() <= prefix.size() || chip.substr(0, prefix.size()) != prefix)
        return {};
    for (const char digit : chip.substr(prefix.size()))
        if (!std::isdigit(static_cast<unsigned char>(digit))) return {};
    return "/dev/" + std::string(chip);
}

PowerOutput power_output_from_name(const std::string &name)
{
    if (name == "GROVE5V" || name == "extport_usb")
        return PowerOutput::Grove5V;
    if (name == "EXT5V" || name == "extport_5vout")
        return PowerOutput::Ext5V;
    if (name == "BACKLIGHT")
        return PowerOutput::Backlight;
    return PowerOutput::Unknown;
}

int switch_value(int value)
{
    return value == 0 ? 0 : 1;
}

int physical_line_value(int logical_value, bool active_low)
{
    return switch_value(logical_value) ^ (active_low ? 1 : 0);
}

int logical_line_value(int physical_value, bool active_low)
{
    return switch_value(physical_value) ^ (active_low ? 1 : 0);
}

} // namespace cp0::settings
