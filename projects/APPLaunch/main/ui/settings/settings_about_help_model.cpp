#include "settings_about_help_model.hpp"

namespace settings_t12b::about_help {
namespace {

std::string value_or_unknown(std::string_view value)
{
    return value.empty() ? "unknown" : std::string(value);
}

} // namespace

Content about(std::string_view version,
              std::string_view build_date,
              std::string_view channel,
              std::string_view commit)
{
    return {
        "About",
        {
            "M5CardputerZero",
            "LVGL: 9.x",
            "Version: " + value_or_unknown(version),
            "Build: " + value_or_unknown(build_date),
            "Channel: " + value_or_unknown(channel),
            "Commit: " + value_or_unknown(commit),
        },
    };
}

Content help()
{
    return {
        "Help",
        {
            "Up/Down: select",
            "Enter/Right: open or apply",
            "Esc/Left: go back",
            "Toggle status is read from the device",
            "A failed operation restores the previous state",
        },
    };
}

} // namespace settings_t12b::about_help
