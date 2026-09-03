/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app_display_order.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace {

constexpr std::array<std::string_view, 24> kBuiltInAppOrder = {
    "settings",
    "store",
    "cli",
    "python",
    "zclaw",
    "ssh",
    "ippanel",
    "files",
    "camera",
    "rec",
    "music",
    "piano",
    "compass",
    "irremote",
    "irchat",
    "calculator",
    "snake",
    "tank",
    "keyboardguide",
    "factorytest",
    "gps",
    "lora",
    "nfc",
    "capcc1101subgchat",
};

// These applications are delivered as independent product packages and are
// therefore not all present in APPLaunch's preinstalled manifest.
constexpr std::array<std::string_view, 15> kManagedDesktopFilenames = {
    "zclaw.desktop",
    "files.desktop",
    "camera_app.desktop",
    "recorder.desktop",
    "music.desktop",
    "piano.desktop",
    "compass.desktop",
    "ir-remote.desktop",
    "ir-chat.desktop",
    "keyboard-guide.desktop",
    "factory_test.desktop",
    "cap-lora-1262-gps.desktop",
    "cap-lora-1262.desktop",
    "cap-cc1101-nfc.desktop",
    "cap-cc1101-subg-chat.desktop",
};

std::string normalized_label(std::string_view label)
{
    std::string normalized;
    normalized.reserve(label.size());
    for (const unsigned char character : label) {
        if (std::isalnum(character))
            normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    return normalized;
}

} // namespace

int launcher_builtin_app_display_order(std::string_view label) noexcept
{
    try {
        const std::string normalized = normalized_label(label);
        for (std::size_t index = 0; index < kBuiltInAppOrder.size(); ++index)
            if (normalized == kBuiltInAppOrder[index]) return static_cast<int>(index);
        // Releases have used both the short and hardware-prefixed SubG name.
        if (normalized == "subgchat") return 23;
    } catch (...) {
        // An allocation failure must not prevent the launcher from showing apps.
    }
    return -1;
}

bool launcher_builtin_desktop_app_is_managed(std::string_view label) noexcept
{
    // The first three entries are APPLaunch's protected pages. The remaining
    // labels are the product-owned app set, including desktop applications.
    const int order = launcher_builtin_app_display_order(label);
    return order >= 3;
}

bool launcher_builtin_desktop_filename_is_managed(std::string_view filename) noexcept
{
    return std::find(kManagedDesktopFilenames.begin(), kManagedDesktopFilenames.end(),
                     filename) != kManagedDesktopFilenames.end();
}
