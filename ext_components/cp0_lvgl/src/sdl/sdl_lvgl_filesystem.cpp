#include "cp0_lvgl_app.h"
#include "cp0_lvgl_filesystem.hpp"
#include "hal/hal_paths.h"
#include "hal_lvgl_bsp.h"
#include "../cp0_filesystem_api.hpp"
#include "../cp0_posix_filesystem.hpp"
#include "../cp0_resource_path_policy.hpp"
#include "../cp0_c_api_boundary.hpp"
#include "../cp0_init_once.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <fstream>
#include <iterator>
#include <list>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
#include <unistd.h>

static char s_data_dir[512] = ".";
static char s_applications_dir[512] = "./applications";
static char s_lock_file[512] = "/tmp/M5CardputerZero-APPLaunch_fcntl.lock";
static char s_font_dir[512] = "./APPLaunch/share/font";
static char s_font_regular[512] = "./APPLaunch/share/font/AlibabaPuHuiTi-3-55-Regular.ttf";
static char s_font_mono[512] = "./APPLaunch/share/font/LiberationMono-Regular.ttf";
static char s_images_dir[512] = "./APPLaunch/share/images";
static char s_audio_dir[512] = "./APPLaunch/share/audio";

extern "C" void hal_paths_init(const char *exe_dir)
{
    if (!exe_dir)
        exe_dir = ".";
    std::snprintf(s_data_dir, sizeof(s_data_dir), "%s", exe_dir);
    std::snprintf(s_applications_dir, sizeof(s_applications_dir), "%s/applications", exe_dir);
    std::snprintf(s_images_dir, sizeof(s_images_dir), "%s/APPLaunch/share/images", exe_dir);
    std::snprintf(s_font_dir, sizeof(s_font_dir), "%s/APPLaunch/share/font", exe_dir);
    std::snprintf(s_audio_dir, sizeof(s_audio_dir), "%s/APPLaunch/share/audio", exe_dir);
    std::snprintf(s_font_regular, sizeof(s_font_regular),
                  "%s/APPLaunch/share/font/AlibabaPuHuiTi-3-55-Regular.ttf", exe_dir);
    std::snprintf(s_font_mono, sizeof(s_font_mono), "%s/APPLaunch/share/font/LiberationMono-Regular.ttf", exe_dir);
}

extern "C" const char *hal_path_data_dir(void) { return s_data_dir; }
extern "C" const char *hal_path_applications_dir(void) { return s_applications_dir; }
extern "C" const char *hal_path_lock_file(void) { return s_lock_file; }
extern "C" const char *hal_path_font_dir(void) { return s_font_dir; }
extern "C" const char *hal_path_font_regular(void) { return s_font_regular; }
extern "C" const char *hal_path_font_mono(void) { return s_font_mono; }
extern "C" const char *hal_path_keyboard_device(void) { return nullptr; }
extern "C" const char *hal_path_keyboard_map(void) { return nullptr; }
extern "C" const char *hal_path_images_dir(void) { return s_images_dir; }
extern "C" const char *hal_path_audio_dir(void) { return s_audio_dir; }

namespace {

static bool starts_with(const std::string &value, const char *prefix)
{
    const std::string p(prefix ? prefix : "");
    return value.compare(0, p.size(), p) == 0;
}

static std::string join_path(const char *base, const std::string &rel)
{
    if (rel.empty())
        return base ? std::string(base) : std::string();
    if (rel.front() == '/' || cp0::filesystem::has_lvgl_drive(rel))
        return rel;
    std::string out = base ? std::string(base) : std::string(".");
    if (!out.empty() && out.back() != '/')
        out.push_back('/');
    out += rel;
    return out;
}

static std::string resource_path(const char *base, const char *prefix, const std::string &file)
{
    std::string rel = cp0::filesystem::strip_app_root_prefix(file);
    if (cp0::filesystem::has_lvgl_drive(rel)) {
        rel = rel.substr(2);
        while (!rel.empty() && rel.front() == '/')
            rel.erase(rel.begin());
    }
    const std::string prefix_with_slash = std::string(prefix) + "/";
    if (starts_with(rel, prefix_with_slash.c_str()))
        rel = rel.substr(prefix_with_slash.size());
    return join_path(base, rel);
}

struct SdlWatcher {
    std::string path;
    time_t mtime = 0;
    off_t size = 0;
    nlink_t nlink = 0;
    bool valid = false;
};

class SdlFilesystem {
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

    void api_call(arg_t arg, callback_t callback)
    {
        cp0_filesystem_api::Operations operations;
        operations.resolve_path = resolve_path;
        operations.ensure_directory = ensure_directory;
        operations.watch_start = [this](const char *path) { return dir_watch_start(path); };
        operations.watch_poll = [this](cp0_watcher_t watcher) { return dir_watch_poll(watcher); };
        operations.watch_stop = [this](cp0_watcher_t watcher) { return dir_watch_stop(watcher); };
        cp0_filesystem_api::dispatch(arg, operations, std::move(callback));
    }

    std::string path(std::string file) { return resolve_path(file); }

    int dir_list(const char *path, cp0_dirent_t *entries, int max_entries, int *out_count)
    {
        return list_dir(path, entries, max_entries, out_count);
    }

    cp0_watcher_t dir_watch_start(const char *path)
    {
        return watchers_.add(native_watch_start(path));
    }
    int dir_watch_poll(cp0_watcher_t watcher)
    {
        return watchers_.poll(watcher, native_watch_poll);
    }
    int dir_watch_stop(cp0_watcher_t watcher)
    {
        return watchers_.stop(watcher, native_watch_stop) ? 0 : -1;
    }

private:
    static int ensure_directory(const std::string &path)
    {
        const int result = mkdir(path.c_str(), 0755);
        struct stat status{};
        return (result == 0 || errno == EEXIST) && stat(path.c_str(), &status) == 0 &&
                S_ISDIR(status.st_mode)
            ? 0
            : -1;
    }

    static std::string resolve_path(const std::string &file)
    {
        if (file.empty())
            return "";
        if (file == "applications")
            return hal_path_applications_dir();
        if (file == "appstore_exec")
            return join_path(hal_path_data_dir(), "bin/M5CardputerZero-AppStore");
        if (file == "calculator_exec")
            return join_path(hal_path_data_dir(), "bin/M5CardputerZero-Calculator");
        if (file == "adb_helper")
            return join_path(hal_path_data_dir(), "adb/cardputer-adb");
        if (file == "launcher_settings")
            return join_path(hal_path_data_dir(), "settings");
        if (file == "oobe_marker")
            return join_path(hal_path_data_dir(), "run-oobe");
        if (file == "home_dir") {
            const char *home = std::getenv("HOME");
            return home && home[0] ? home : ".";
        }
        if (file == "lock_file")
            return hal_path_lock_file();
        if (file == "keyboard_device")
            return hal_path_keyboard_device() ? hal_path_keyboard_device() : "";
        if (file == "keyboard_map")
            return hal_path_keyboard_map() ? hal_path_keyboard_map() : "";

        const cp0::filesystem::ResourceKind kind = cp0::filesystem::classify_resource(file);
        if (kind != cp0::filesystem::ResourceKind::none &&
            cp0::filesystem::has_parent_component(file))
            return "";
        if (kind == cp0::filesystem::ResourceKind::image)
            return resource_path(hal_path_images_dir(), "share/images", file);
        if (kind == cp0::filesystem::ResourceKind::audio)
            return resource_path(hal_path_audio_dir(), "share/audio", file);
        if (kind == cp0::filesystem::ResourceKind::font)
            return resource_path(hal_path_font_dir(), "share/font", file);
        if (cp0::filesystem::has_lvgl_drive(file)) {
            std::string rel = file.substr(2);
            while (!rel.empty() && rel.front() == '/')
                rel.erase(rel.begin());
            return rel;
        }
        return file;
    }

    static int list_dir(const char *path, cp0_dirent_t *entries, int max_entries, int *out_count)
    {
        return cp0_posix_filesystem::list_directory(path, entries, max_entries, out_count, true);
    }

    static void snapshot(SdlWatcher *watcher)
    {
        if (!watcher)
            return;
        struct stat st;
        if (stat(watcher->path.c_str(), &st) == 0) {
            watcher->mtime = st.st_mtime;
            watcher->size = st.st_size;
            watcher->nlink = st.st_nlink;
            watcher->valid = true;
        } else {
            watcher->valid = false;
        }
    }

    static cp0_watcher_t native_watch_start(const char *path)
    {
        if (!path || !path[0])
            return nullptr;
        SdlWatcher *watcher = new SdlWatcher();
        watcher->path = path;
        snapshot(watcher);
        return watcher;
    }

    static int native_watch_poll(cp0_watcher_t handle)
    {
        if (!handle)
            return -1;
        SdlWatcher *watcher = static_cast<SdlWatcher *>(handle);
        SdlWatcher now;
        now.path = watcher->path;
        snapshot(&now);
        const bool changed = now.valid != watcher->valid || now.mtime != watcher->mtime ||
                             now.size != watcher->size || now.nlink != watcher->nlink;
        *watcher = now;
        return changed ? 1 : 0;
    }

    static void native_watch_stop(cp0_watcher_t watcher)
    {
        delete static_cast<SdlWatcher *>(watcher);
    }

    cp0_filesystem_api::WatcherRegistry watchers_;
};

SdlFilesystem &filesystem()
{
    static SdlFilesystem instance;
    return instance;
}

} // namespace

std::string cp0_file_path(std::string file)
{
    return filesystem().path(std::move(file));
}

extern "C" const char *cp0_file_path_c(const char *file)
{
    return cp0::invoke_c_api_or("", [file] {
        static thread_local std::unordered_map<std::string, std::string> paths;
        std::string key = file ? file : "";
        auto it = paths.find(key);
        if (it == paths.end())
            it = paths.emplace(key, cp0_file_path(key)).first;
        return it->second.c_str();
    });
}

extern "C" int cp0_dir_list(const char *path, cp0_dirent_t *entries, int max_entries, int *out_count)
{
    return filesystem().dir_list(path, entries, max_entries, out_count);
}

extern "C" cp0_watcher_t cp0_dir_watch_start(const char *path)
{
    return filesystem().dir_watch_start(path);
}

extern "C" int cp0_dir_watch_poll(cp0_watcher_t watcher)
{
    return filesystem().dir_watch_poll(watcher);
}

extern "C" void cp0_dir_watch_stop(cp0_watcher_t watcher)
{
    (void)filesystem().dir_watch_stop(watcher);
}

extern "C" void init_filesystem(void)
{
    static cp0::InitOnce initialized;
    initialized.run([] {
        return static_cast<bool>(cp0_signal_filesystem_api.append(
            [](std::list<std::string> arg,
               std::function<void(int, std::string)> callback) {
                filesystem().api_call(std::move(arg), std::move(callback));
            }));
    });
}
