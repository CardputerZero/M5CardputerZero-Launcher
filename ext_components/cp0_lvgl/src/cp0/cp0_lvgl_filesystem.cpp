#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "../cp0_filesystem_api.hpp"
#include "../cp0_posix_filesystem.hpp"
#include "../cp0_resource_path_policy.hpp"
#include "../cp0_c_api_boundary.hpp"
#include "../cp0_init_once.hpp"
#include "../cp0_integer_codec.hpp"

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
#include <sys/inotify.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace {
class Cp0Filesystem {
public:
    void api_call(const std::list<std::string> &arg, std::function<void(int, std::string)> callback)
    {
        cp0_filesystem_api::Operations operations;
        operations.resolve_path = resolve_path;
        operations.ensure_directory = ensure_directory;
        operations.watch_start = [this](const char *path) { return dir_watch_start(path); };
        operations.watch_poll = [this](cp0_watcher_t watcher) { return dir_watch_poll(watcher); };
        operations.watch_stop = [this](cp0_watcher_t watcher) { return dir_watch_stop(watcher); };
        operations.detail_errno_on_open_failure = false;
        cp0_filesystem_api::dispatch(arg, operations, std::move(callback));
    }

    std::string path(std::string file)
    {
        int code = -1;
        std::string data;
        invoke({"Path", std::move(file)}, code, data);
        return code == 0 ? data : std::string();
    }

    int dir_list(const char *path, cp0_dirent_t *entries, int max_entries, int *out_count)
    {
        int code = -1;
        std::string data;
        invoke({"DirList", path ? path : ""}, code, data);
        if (code != 0) return code;
        return decode_dir_entries(data, entries, max_entries, out_count);
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
    struct Watcher {
        int inotify_fd;
        int watch_fd;
    };

    static constexpr const char *kAppRoot = "/usr/share/APPLaunch";
    static constexpr const char *kLvglFsRoot = "A:/";

    void invoke(std::list<std::string> arg, int &code, std::string &data)
    {
        api_call(arg, [&](int c, std::string d) {
            code = c;
            data = std::move(d);
        });
    }

    static int ensure_directory(const std::string &path)
    {
        uid_t uid = 0;
        gid_t gid = 0;
        if (getuid() == 0) {
            std::uintmax_t parsed_uid = 0;
            std::uintmax_t parsed_gid = 0;
            const std::uintmax_t invalid_uid =
                static_cast<std::uintmax_t>(std::numeric_limits<uid_t>::max());
            const std::uintmax_t invalid_gid =
                static_cast<std::uintmax_t>(std::numeric_limits<gid_t>::max());
            if (!cp0_integer_codec::parse_non_root_identity(
                    std::getenv("SUDO_UID"), invalid_uid, parsed_uid) ||
                !cp0_integer_codec::parse_non_root_identity(
                    std::getenv("SUDO_GID"), invalid_gid, parsed_gid))
                return -1;
            uid = static_cast<uid_t>(parsed_uid);
            gid = static_cast<gid_t>(parsed_gid);
        }

        const int result = mkdir(path.c_str(), 0755);
        struct stat status{};
        if ((result != 0 && errno != EEXIST) || stat(path.c_str(), &status) != 0 ||
            !S_ISDIR(status.st_mode)) {
            return -1;
        }
        if (getuid() != 0) return 0;
        return chown(path.c_str(), uid, gid) == 0 ? 0 : -1;
    }

    static bool starts_with(const std::string &value, const char *prefix)
    {
        const std::string prefix_str(prefix);
        return value.compare(0, prefix_str.size(), prefix_str) == 0;
    }

    static std::string lvgl_root_path(std::string rel)
    {
        while (!rel.empty() && rel.front() == '/') {
            rel.erase(rel.begin());
        }
        return std::string(kLvglFsRoot) + rel;
    }

    static std::string resolve_lvgl_image_path(const std::string &file)
    {
        if (cp0::filesystem::has_lvgl_drive(file)) return file;

        const std::string rel = cp0::filesystem::strip_app_root_prefix(file);

        if (!rel.empty() && rel.front() == '/') return rel;
        if (starts_with(rel, "share/images/")) return lvgl_root_path(rel);

        return lvgl_root_path("share/images/" + rel);
    }

    static std::string resolve_native_resource_path(const std::string &directory,
                                                    const char *prefix,
                                                    const std::string &file)
    {
        std::string relative = cp0::filesystem::strip_app_root_prefix(file);
        if (!relative.empty() && relative.front() == '/') return relative;
        const std::string resource_prefix = std::string(prefix) + "/";
        if (starts_with(relative, resource_prefix.c_str()))
            relative.erase(0, resource_prefix.size());
        return directory + "/" + relative;
    }

    static std::string resolve_path(const std::string &file)
    {
        if (file.empty()) return "";

        if (file == "applications") return std::string(kAppRoot) + "/applications";
        if (file == "appstore_exec") return std::string(kAppRoot) + "/bin/M5CardputerZero-AppStore";
        if (file == "calculator_exec") return std::string(kAppRoot) + "/bin/M5CardputerZero-Calculator";
        if (file == "adb_helper") return std::string(kAppRoot) + "/adb/cardputer-adb";
        if (file == "launcher_settings") return "/var/lib/applaunch/settings";
        if (file == "oobe_marker") return "/var/lib/applaunch/run-oobe";
        if (file == "home_dir") {
            const char *home = std::getenv("HOME");
            return home && home[0] ? home : "/tmp";
        }
        if (file == "lock_file") return "/tmp/M5CardputerZero-APPLaunch_fcntl.lock";
        if (file == "keyboard_device") return "/dev/input/by-path/platform-3f804000.i2c-event";
        if (file == "keyboard_map") return "/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map";

        const cp0::filesystem::ResourceKind kind = cp0::filesystem::classify_resource(file);
        if (kind != cp0::filesystem::ResourceKind::none &&
            cp0::filesystem::has_parent_component(file))
            return "";
        if (kind == cp0::filesystem::ResourceKind::image) return resolve_lvgl_image_path(file);
        if (kind == cp0::filesystem::ResourceKind::audio)
            return resolve_native_resource_path(
                std::string(kAppRoot) + "/share/audio", "share/audio", file);
        if (kind == cp0::filesystem::ResourceKind::font)
            return resolve_native_resource_path(
                std::string(kAppRoot) + "/share/font", "share/font", file);

        return file;
    }

    static int list_dir(const char *path, cp0_dirent_t *entries, int max_entries, int *out_count)
    {
        return cp0_posix_filesystem::list_directory(path, entries, max_entries, out_count, false);
    }

    static int decode_dir_entries(const std::string &data, cp0_dirent_t *entries, int max_entries, int *out_count)
    {
        if (out_count) *out_count = 0;
        if (!entries || max_entries <= 0 || !out_count) return 0;

        size_t start = 0;
        while (start < data.size() && *out_count < max_entries) {
            size_t end = data.find('\n', start);
            std::string line = data.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (line.size() >= 3 && line[1] == '\t') {
                entries[*out_count].is_dir = (line[0] == 'D') ? 1 : 0;
                std::string name;
                if (!decode_field(line.substr(2), name)) {
                    if (end == std::string::npos) break;
                    start = end + 1;
                    continue;
                }
                std::strncpy(entries[*out_count].name, name.c_str(), sizeof(entries[*out_count].name) - 1);
                entries[*out_count].name[sizeof(entries[*out_count].name) - 1] = '\0';
                (*out_count)++;
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return 0;
    }

    static bool decode_field(const std::string &encoded, std::string &decoded)
    {
        auto hex_value = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        decoded.clear();
        for (size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i] != '%') { decoded.push_back(encoded[i]); continue; }
            if (i + 2 >= encoded.size()) return false;
            const int high = hex_value(encoded[i + 1]);
            const int low = hex_value(encoded[i + 2]);
            if (high < 0 || low < 0) return false;
            decoded.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        }
        return true;
    }

    static cp0_watcher_t native_watch_start(const char *path)
    {
        if (!path) return nullptr;

        int fd = inotify_init1(IN_NONBLOCK);
        if (fd < 0) return nullptr;

        int wd = inotify_add_watch(fd, path, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
        if (wd < 0) {
            close(fd);
            return nullptr;
        }

        auto *watcher = static_cast<Watcher *>(std::malloc(sizeof(Watcher)));
        if (!watcher) {
            close(fd);
            return nullptr;
        }
        watcher->inotify_fd = fd;
        watcher->watch_fd = wd;
        return watcher;
    }

    static int native_watch_poll(cp0_watcher_t watcher)
    {
        if (!watcher) return -1;

        auto *w = static_cast<Watcher *>(watcher);
        char buf[1024] __attribute__((aligned(8)));
        ssize_t n = read(w->inotify_fd, buf, sizeof(buf));
        return (n > 0) ? 1 : 0;
    }

    static void native_watch_stop(cp0_watcher_t watcher)
    {
        if (!watcher) return;

        auto *w = static_cast<Watcher *>(watcher);
        if (w->watch_fd >= 0) inotify_rm_watch(w->inotify_fd, w->watch_fd);
        close(w->inotify_fd);
        std::free(w);
    }

    cp0_filesystem_api::WatcherRegistry watchers_;
};

Cp0Filesystem &filesystem()
{
    static Cp0Filesystem instance;
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
        std::string key = file ? std::string(file) : std::string();
        auto it = paths.find(key);
        if (it == paths.end()) it = paths.emplace(key, cp0_file_path(key)).first;
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
                filesystem().api_call(arg, std::move(callback));
            }));
    });
}
