#include "desktop_app_loader.hpp"

#include "builtin_app_registry.hpp"
#include "app_registry.h"
#include "cp0_lvgl_app.h"
#include "desktop_entry.h"
#include "launch.h"
#include "launcher_platform.hpp"

#include <cstdio>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace {

bool contains_exec(const std::list<app> &apps, const std::string &exec)
{
    for (const auto &item : apps) {
        if (item.Exec == exec) return true;
    }
    return false;
}

std::string desktop_config_key(const std::string &filename)
{
    std::string key = "app_desktop_";
    for (const unsigned char character : filename) {
        if (std::isalnum(character)) key.push_back(static_cast<char>(character));
        else key.push_back('_');
    }

    uint32_t hash = 2166136261u;
    for (const unsigned char character : filename) {
        hash ^= character;
        hash *= 16777619u;
    }
    std::ostringstream suffix;
    suffix << '_' << std::hex << std::setw(8) << std::setfill('0') << hash;
    key += suffix.str();
    return key;
}

} // namespace

void launcher_append_desktop_apps(std::list<app> &apps)
{
    const std::size_t initial_size = apps.size();
    launcher_app_registry_begin_dynamic_refresh();
    try {
    const std::string app_dir = launcher_platform::path("applications");
    if (app_dir.empty()) return;
    int list_code = -1;
    std::string listing;
    cp0_signal_filesystem_api({"DirList", app_dir}, [&](int code, std::string data) {
        list_code = code;
        listing = std::move(data);
    });
    if (list_code != 0) return;

    std::istringstream lines(listing);
    std::string line;
    std::size_t appended = 0;
    while (std::getline(lines, line)) {
        if (appended >= LAUNCHER_MAX_DESKTOP_APPS) break;
        if (line.size() < 3 || line[0] != 'F' || line[1] != '\t') continue;

        std::string name;
        if (!launcher_platform::decode_field(line.substr(2), name) ||
            !desktop_entry_filename_valid(name))
            continue;

        const std::string path = app_dir + "/" + name;
        int read_code = -1;
        std::string desktop_data;
        cp0_signal_filesystem_api({"ReadFile", path, "65536"}, [&](int code, std::string data) {
            read_code = code;
            desktop_data = std::move(data);
        });
        if (read_code != 0) {
            std::fprintf(stderr, "applications_load: cannot open %s\n", path.c_str());
            continue;
        }

        const std::optional<DesktopEntry> entry = parse_desktop_entry(desktop_data);
        if (!entry) {
            std::fprintf(stderr, "applications_load: skip %s (missing Name or Exec)\n", path.c_str());
            continue;
        }

        int safe_code = -1;
        std::string unsafe_reason;
        cp0_signal_process_api({"DesktopExecIsSafe", entry->exec}, [&](int code, std::string data) {
            safe_code = code;
            unsafe_reason = std::move(data);
        });
        if (safe_code != 0) {
            std::fprintf(stderr, "applications_load: skip %s (unsafe Exec: %s)\n",
                         path.c_str(), unsafe_reason.c_str());
            continue;
        }
        if (contains_exec(apps, entry->exec)) {
            std::fprintf(stderr, "applications_load: skip %s (duplicate Exec)\n", path.c_str());
            continue;
        }
        if (launcher_builtin_app_owns_exec(entry->exec)) {
            std::fprintf(stderr, "applications_load: skip %s (shadows built-in app)\n", path.c_str());
            continue;
        }

        const std::string icon_path = launcher_platform::path(entry->icon);
        if (!entry->icon.empty() && icon_path.empty()) {
            std::fprintf(stderr, "applications_load: skip %s (invalid Icon)\n", path.c_str());
            continue;
        }

        const AppDescriptor *descriptor = launcher_app_registry_register_dynamic(
            entry->name, icon_path, desktop_config_key(name));
        if (!descriptor) continue;
        bool enabled = true;
        try {
            enabled = launcher_app_registry_is_enabled(*descriptor);
        } catch (...) {
            enabled = true;
        }
        if (!enabled) continue;

        apps.emplace_back(entry->name, icon_path, entry->exec,
                          entry->terminal, entry->sysplause);
        ++appended;
    }
    } catch (...) {
        while (apps.size() > initial_size) apps.pop_back();
    }
}
