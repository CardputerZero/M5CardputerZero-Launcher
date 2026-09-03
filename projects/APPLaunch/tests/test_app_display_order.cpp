#include "../main/ui/app_display_order.hpp"

#include <array>
#include <cassert>
#include <string_view>

int main()
{
    constexpr std::array<std::string_view, 23> expected = {
        "Settings", "Store", "CLI", "Python", "ZClaw", "SSH", "IP Panel", "Files",
        "Camera", "Rec", "Music", "Piano", "Compass", "IR Remote", "IR Chat",
        "Calculator", "Snake", "Tank", "Keyboard Guide", "FactoryTest", "GPS", "NFC",
        "Cap-CC1101-SubG-Chat",
    };

    for (std::size_t index = 0; index < expected.size(); ++index)
        assert(launcher_builtin_app_display_order(expected[index]) == static_cast<int>(index));

    // Desktop files in the existing image use this spelling for IP Panel.
    assert(launcher_builtin_app_display_order("IP_PANEL") == 6);
    assert(launcher_builtin_app_display_order("Vim") == -1);
    return 0;
}
