#include "desktop_app_loader.hpp"

#include "builtin_app_registry.hpp"
#include "app_registry.h"
#include "cp0_lvgl_app.h"
#include "desktop_entry.h"
#include "launch.h"
#include "launcher_platform.hpp"
#include "model/preinstalled_app_manifest.hpp"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct DesktopAppCandidate
{
    std::string filename;
    DesktopEntry entry;
    std::string icon_path;
    std::filesystem::file_time_type::rep modified_time{};
    bool has_modified_time = false;
    std::size_t directory_sequence = 0;
};

bool contains_exec(const std::list<app> &apps, const std::string &exec)
{
    for (const auto &item : apps)
        if (item.Exec == exec) return true;
    return false;
}

void sort_desktop_candidates(std::vector<DesktopAppCandidate> &candidates)
{
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        if (left.has_modified_time != right.has_modified_time)
            return left.has_modified_time;
        if (left.has_modified_time && left.modified_time != right.modified_time)
            return left.modified_time < right.modified_time;
        return left.directory_sequence < right.directory_sequence;
    });
}

bool read_modified_time(const std::string &path,
                        std::filesystem::file_time_type::rep &modified_time)
{
    std::error_code error;
    const auto timestamp = std::filesystem::last_write_time(path, error);
    if (error) return false;
    modified_time = timestamp.time_since_epoch().count();
    return true;
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

std::vector<PreinstalledDesktopApp> load_preinstalled_app_manifest(
    const std::string &applications_dir)
{
    const std::filesystem::path path =
        std::filesystem::path(applications_dir).parent_path() /
        "preinstalled-desktop-apps.tsv";
    if (applications_dir.empty() || path.empty()) return {};

    int read_code = -1;
    std::string contents;
    cp0_signal_filesystem_api({"ReadFile", path.string(), "65536"},
                              [&](int code, std::string data) {
                                  read_code = code;
                                  contents = std::move(data);
                              });
    if (read_code != 0) return {};
    return parse_preinstalled_app_manifest(contents);
}

} // namespace

void launcher_append_desktop_apps(std::list<app> &apps)
{
    const std::size_t initial_size = apps.size();
    bool refresh_started = false;
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

        launcher_app_registry_begin_dynamic_refresh();
        refresh_started = true;
        const auto preinstalled_apps = load_preinstalled_app_manifest(app_dir);

        std::istringstream lines(listing);
        std::string line;
        std::size_t directory_sequence = 0;
        std::vector<DesktopAppCandidate> candidates;
        while (std::getline(lines, line)) {
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

            DesktopAppCandidate candidate;
            candidate.filename = std::move(name);
            candidate.entry = std::move(*entry);
            candidate.icon_path = icon_path;
            // AppStore rewrites the desktop file after installation, so its
            // last-write time is the install/update order for custom apps.
            candidate.has_modified_time = read_modified_time(path, candidate.modified_time);
            candidate.directory_sequence = directory_sequence++;
            candidates.push_back(std::move(candidate));
        }

        sort_desktop_candidates(candidates);
        std::size_t appended = 0;
        std::unordered_set<std::string> registered_execs;
        for (auto &candidate : candidates) {
            if (contains_exec(apps, candidate.entry.exec)) {
                std::fprintf(stderr, "applications_load: skip duplicate Exec %s\n",
                             candidate.entry.exec.c_str());
                continue;
            }
            if (!registered_execs.insert(candidate.entry.exec).second) {
                std::fprintf(stderr, "applications_load: skip duplicate Exec %s\n",
                             candidate.entry.exec.c_str());
                continue;
            }

            const LauncherAppOrigin origin = preinstalled_app_manifest_contains(
                preinstalled_apps, candidate.filename, candidate.entry)
                ? LauncherAppOrigin::Preinstalled
                : LauncherAppOrigin::StoreInstalled;
            const std::string config_key = desktop_config_key(candidate.filename);
            if (!launcher_app_registry_register_dynamic(
                    candidate.entry.name, candidate.icon_path, config_key, origin))
                continue;
            const bool settings_managed = origin == LauncherAppOrigin::Preinstalled;
            const AppDescriptor descriptor{
                candidate.entry.name.c_str(), candidate.icon_path.c_str(),
                config_key.c_str(), settings_managed, !settings_managed, origin};
            bool enabled = true;
            try {
                enabled = launcher_app_registry_is_enabled(descriptor);
            } catch (...) {
                enabled = true;
            }
            if (!enabled || appended >= LAUNCHER_MAX_DESKTOP_APPS) continue;

            apps.emplace_back(candidate.entry.name, candidate.icon_path, candidate.entry.exec,
                              candidate.entry.terminal, candidate.entry.sysplause);
            ++appended;
        }
        launcher_app_registry_commit_dynamic_refresh();
        refresh_started = false;
    } catch (...) {
        while (apps.size() > initial_size) apps.pop_back();
        if (refresh_started) launcher_app_registry_cancel_dynamic_refresh();
    }
}
