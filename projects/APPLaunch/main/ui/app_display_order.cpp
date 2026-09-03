/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app_display_order.hpp"

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
