/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string_view>

// Returns the zero-based position in the product's built-in app order, or -1
// when the label belongs to an installable application.
int launcher_builtin_app_display_order(std::string_view label) noexcept;

// Returns true for product-owned desktop applications that belong in the
// Settings -> Launcher control list. Settings, Store, and CLI are protected
// launcher surfaces and are intentionally excluded.
bool launcher_builtin_desktop_app_is_managed(std::string_view label) noexcept;

// Returns true for desktop filenames shipped by the CardputerZero product
// packages.  This is used together with the desktop entry identity so a
// user-installed app with a matching display name is not treated as built-in.
bool launcher_builtin_desktop_filename_is_managed(std::string_view filename) noexcept;
