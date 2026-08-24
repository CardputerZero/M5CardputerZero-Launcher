#pragma once

#include <string_view>

namespace settings_t12b::extport {

enum class Port {
    Grove5V,
    Ext5V,
};

struct PortSpec
{
    std::string_view label;
    std::string_view api_name;
};

const PortSpec &spec(Port port) noexcept;
bool parse_logical_value(std::string_view payload, bool &value) noexcept;
bool gpio_set_succeeded(int code, std::string_view payload) noexcept;
bool state_after_write(bool previous, bool requested, bool succeeded) noexcept;
int physical_line_value(bool logical_value, bool active_low) noexcept;
int logical_line_value(int physical_value, bool active_low) noexcept;

} // namespace settings_t12b::extport
