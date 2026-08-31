#pragma once

#include <list>
#include <string>

struct app;
struct AppDescriptor;

void launcher_append_enabled_builtin_apps(std::list<app> &apps);
bool launcher_builtin_app_owns_exec(const std::string &exec);

void launcher_app_registry_begin_dynamic_refresh();
void launcher_app_registry_commit_dynamic_refresh();
void launcher_app_registry_cancel_dynamic_refresh();
const AppDescriptor *launcher_app_registry_register_dynamic(const std::string &label,
                                                            const std::string &icon,
                                                            const std::string &config_key);
