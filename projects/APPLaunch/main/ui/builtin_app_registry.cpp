#include "builtin_app_registry.hpp"

#include "app_display_order.hpp"
#include "app_registry.h"
#include "launch.h"
#include "ui.h"
#include "generated/page_app.h"
#include "launcher_platform.hpp"
#include "model/dynamic_app_registry.hpp"
#include "settings/settings_page.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using BuiltinAppAppender = void (*)(std::list<app> &apps, const AppDescriptor &desc);

struct BuiltinAppRegistration {
    AppDescriptor desc;
    const char *exec;
    bool terminal;
    bool sysplause;
    bool run_as_root;
    BuiltinAppAppender append;
};

template <class PageT>
void append_page_app(std::list<app> &apps, const AppDescriptor &desc)
{
    apps.emplace_back(desc.label, launcher_platform::path(desc.icon), page_v<PageT>);
}

std::string resolved_exec(const BuiltinAppRegistration &registration)
{
    std::string exec = registration.exec ? registration.exec : "";
    if (!exec.empty() && exec.front() == '@') return launcher_platform::path(exec.substr(1));
    return exec;
}

void append_builtin_app(std::list<app> &apps, const BuiltinAppRegistration &registration)
{
    if (registration.append) {
        registration.append(apps, registration.desc);
        return;
    }
    apps.emplace_back(registration.desc.label,
                      launcher_platform::path(registration.desc.icon),
                      resolved_exec(registration),
                      registration.terminal,
                      registration.sysplause,
                      registration.run_as_root);
}

constexpr BuiltinAppRegistration BUILTIN_APPS[] = {
    {{"Settings", "setting_100.png", "app_Setting", false, true},
     nullptr, false, true, false, append_page_app<UISettingTreePage>},
    {{"Store", "store_100.png", "app_Store", false, true},
     "@appstore_exec", false, true, false, nullptr},
    {{"CLI", "cli_100.png", "app_CLI", false, true},
     nullptr, false, true, false, append_page_app<UISTPage>},
    {{"Python", "python_100.png", "app_Python", false, true}, "python3", true, false, false, nullptr},
#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)
    {{"SSH", "ssh_100.png", "app_SSH", true, false},
     nullptr, false, true, false, append_page_app<UISSHPage>},
    {{"IP Panel", "ip_panel_100.png", "app_IP_Panel", true, false},
     nullptr, false, true, false, append_page_app<UIIpPanelPage>},
#endif
    {{"Calculator", "math_100.png", "app_Math", true, false},
     "@calculator_exec", false, true, false, nullptr},
    {{"Snake", "game_100.png", "app_Game", false, true},
     nullptr, false, true, false, append_page_app<UIGamePage>},
    {{"Tank", "tank_100.png", "app_Tank", true, false},
     nullptr, false, true, false, append_page_app<UITankBattlePage>},
};

DynamicAppRegistry &dynamic_app_registry()
{
    static DynamicAppRegistry registry;
    return registry;
}

bool is_first_registration(std::size_t index)
{
    const char *id = BUILTIN_APPS[index].desc.config_key;
    if (!id || !id[0]) return false;
    for (std::size_t previous = 0; previous < index; ++previous) {
        const char *previous_id = BUILTIN_APPS[previous].desc.config_key;
        if (previous_id && std::strcmp(id, previous_id) == 0) return false;
    }
    return true;
}

} // namespace

const AppDescriptor *launcher_app_registry_entries(std::size_t *count)
{
    constexpr std::size_t descriptor_count = sizeof(BUILTIN_APPS) / sizeof(BUILTIN_APPS[0]);
    static std::vector<AppDescriptor> descriptors;
    descriptors.clear();
    const auto &dynamic_entries = dynamic_app_registry().entries();
    descriptors.reserve(descriptor_count + dynamic_entries.size());
    for (std::size_t i = 0; i < descriptor_count; ++i)
        if (is_first_registration(i)) descriptors.push_back(BUILTIN_APPS[i].desc);
    for (const auto &registration : dynamic_entries)
        descriptors.push_back(registration.desc);
    if (count) *count = descriptors.size();
    return descriptors.data();
}

void launcher_app_registry_begin_dynamic_refresh()
{
    dynamic_app_registry().begin_refresh();
}

void launcher_app_registry_commit_dynamic_refresh()
{
    dynamic_app_registry().commit_refresh();
}

void launcher_app_registry_cancel_dynamic_refresh()
{
    dynamic_app_registry().cancel_refresh();
}

const AppDescriptor *launcher_app_registry_register_dynamic(const std::string &label,
                                                            const std::string &icon,
                                                            const std::string &config_key)
{
    return dynamic_app_registry().register_pending(label, icon, config_key);
}

void launcher_append_enabled_builtin_apps(std::list<app> &apps)
{
    for (std::size_t index = 0; index < sizeof(BUILTIN_APPS) / sizeof(BUILTIN_APPS[0]); ++index) {
        if (!is_first_registration(index)) continue;
        const auto &registration = BUILTIN_APPS[index];
        if (launcher_app_registry_is_enabled(registration.desc)) append_builtin_app(apps, registration);
    }
}

void launcher_sort_app_display_order(std::list<app> &apps)
{
    constexpr int installable_app_order = std::numeric_limits<int>::max();
    apps.sort([](const app &left, const app &right) {
        const int left_order = launcher_builtin_app_display_order(left.Name);
        const int right_order = launcher_builtin_app_display_order(right.Name);
        const int normalized_left = left_order >= 0 ? left_order : installable_app_order;
        const int normalized_right = right_order >= 0 ? right_order : installable_app_order;
        return normalized_left < normalized_right;
    });
}

bool launcher_builtin_app_owns_exec(const std::string &exec)
{
    for (const auto &registration : BUILTIN_APPS) {
        const std::string builtin_exec = resolved_exec(registration);
        if (!builtin_exec.empty() && exec == builtin_exec) return true;
    }
    return false;
}
