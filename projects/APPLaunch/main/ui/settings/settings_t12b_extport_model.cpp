#include "settings_t12b_extport_model.hpp"

namespace settings_t12b::extport {
namespace {

constexpr PortSpec kGrove5V{"GROVE5V", "GROVE5V"};
constexpr PortSpec kExt5V{"EXT5V", "EXT5V"};

} // namespace

const PortSpec &spec(Port port) noexcept
{
    return port == Port::Grove5V ? kGrove5V : kExt5V;
}

bool parse_logical_value(std::string_view payload, bool &value) noexcept
{
    if (payload == "0") {
        value = false;
        return true;
    }
    if (payload == "1") {
        value = true;
        return true;
    }
    return false;
}

bool gpio_set_succeeded(int code, std::string_view payload) noexcept
{
    return code == 0 && payload == "ok";
}

bool state_after_write(bool previous, bool requested, bool succeeded) noexcept
{
    return succeeded ? requested : previous;
}

int physical_line_value(bool logical_value, bool active_low) noexcept
{
    return static_cast<int>(logical_value) ^ static_cast<int>(active_low);
}

int logical_line_value(int physical_value, bool active_low) noexcept
{
    return (physical_value != 0 ? 1 : 0) ^ static_cast<int>(active_low);
}

} // namespace settings_t12b::extport
