/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

enum class LauncherAppOrigin : std::uint8_t {
    Builtin,
    Preinstalled,
    StoreInstalled,
};

struct AppDescriptor {
    const char *label;
    const char *icon;
    const char *config_key;
    bool configurable;
    bool always_on;
    LauncherAppOrigin origin = LauncherAppOrigin::Builtin;
};

const AppDescriptor *launcher_app_registry_entries(std::size_t *count);
bool launcher_app_registry_is_enabled(const AppDescriptor &desc);
bool launcher_app_registry_set_enabled(const AppDescriptor &desc, bool enabled);

typedef void (*LauncherAppRegistryChangedCallback)(void *user_data);
void launcher_app_registry_set_changed_callback(LauncherAppRegistryChangedCallback callback,
                                                void *user_data);
bool launcher_app_registry_clear_changed_callback(LauncherAppRegistryChangedCallback callback,
                                                  void *user_data);
void launcher_app_registry_notify_changed();
