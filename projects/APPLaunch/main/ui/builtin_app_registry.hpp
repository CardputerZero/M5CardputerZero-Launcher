#pragma once

#include <cstddef>
#include <list>
#include <string>

#include "app_registry.h"

struct app;

void launcher_append_enabled_builtin_apps(std::list<app> &apps);
void launcher_sort_app_display_order(std::list<app> &apps);
bool launcher_builtin_app_owns_exec(const std::string &exec);
const AppDescriptor *launcher_builtin_app_registry_entries(std::size_t *count);

void launcher_app_registry_begin_dynamic_refresh();
void launcher_app_registry_commit_dynamic_refresh();
void launcher_app_registry_cancel_dynamic_refresh();
bool launcher_app_registry_register_dynamic(const std::string &label,
                                            const std::string &icon,
                                            const std::string &config_key,
                                            LauncherAppOrigin origin);
