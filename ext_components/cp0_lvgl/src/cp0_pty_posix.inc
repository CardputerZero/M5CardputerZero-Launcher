#include "hal_lvgl_bsp.h"
#include "cp0_pty_contract.hpp"
#include "cp0_signal_registration.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <unordered_map>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#define CP0_HAS_POSIX_PTY 1
#endif

#if defined(__linux__)
#include <pty.h>
#elif defined(__APPLE__)
#include <util.h>
#endif

namespace {

typedef void *pty_handle_t;

struct cp0_pty_handle {
    int master_fd;
    pid_t child_pid;
    bool child_reaped;
    int exit_status;
};

class PtySystem {
public:
    typedef std::function<void(int, std::string)> callback_t;
    typedef std::list<std::string> arg_t;

    ~PtySystem()
    {
        shutdown();
    }

    pty_handle_t open(const char *cmd, const char *const *args, int cols, int rows)
    {
#if defined(CP0_HAS_POSIX_PTY)
        if (!cmd || !cmd[0]) return NULL;

        int master_fd = -1;
        struct winsize ws = {};
        ws.ws_col = cols;
        ws.ws_row = rows;
        std::string run_as_user = config_get_str("run_as_user", "");

        pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
        if (pid < 0) return NULL;

        if (pid == 0) {
            setenv("TERM", "vt100", 1);
            drop_root_user(run_as_user);

            if (args)
                execvp(cmd, const_cast<char *const *>(args));
            else
                execlp(cmd, cmd, static_cast<char *>(NULL));
            _exit(127);
        }

        int flags = fcntl(master_fd, F_GETFL);
        if (flags >= 0) fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

        cp0_pty_handle *pty = static_cast<cp0_pty_handle *>(std::malloc(sizeof(cp0_pty_handle)));
        if (!pty) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            ::close(master_fd);
            return NULL;
        }
        pty->master_fd = master_fd;
        pty->child_pid = pid;
        pty->child_reaped = false;
        pty->exit_status = 0;
        {
            std::lock_guard<std::mutex> lock(handles_mutex_);
            if (!accepting_) {
                cleanup(pty);
                return NULL;
            }
            try {
                handles_.emplace(reinterpret_cast<uintptr_t>(pty), pty);
            } catch (...) {
                cleanup(pty);
                return NULL;
            }
        }
        return pty;
#else
        (void)cmd;
        (void)args;
        (void)cols;
        (void)rows;
        return NULL;
#endif
    }

    int read(pty_handle_t pty, char *buf, size_t buf_size)
    {
#if defined(CP0_HAS_POSIX_PTY)
        std::lock_guard<std::mutex> lock(handles_mutex_);
        cp0_pty_handle *h = resolve(pty);
        if (!h || !buf || buf_size == 0) return -1;
        ssize_t n = ::read(h->master_fd, buf, buf_size);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        return static_cast<int>(n);
#else
        (void)pty;
        (void)buf;
        (void)buf_size;
        return -1;
#endif
    }

    int write(pty_handle_t pty, const char *buf, size_t len)
    {
#if defined(CP0_HAS_POSIX_PTY)
        std::lock_guard<std::mutex> lock(handles_mutex_);
        cp0_pty_handle *h = resolve(pty);
        if (!h || !buf) return -1;
        return static_cast<int>(::write(h->master_fd, buf, len));
#else
        (void)pty;
        (void)buf;
        (void)len;
        return -1;
#endif
    }

    int check_child(pty_handle_t pty, int *exit_status)
    {
#if defined(CP0_HAS_POSIX_PTY)
        std::lock_guard<std::mutex> lock(handles_mutex_);
        cp0_pty_handle *h = resolve(pty);
        if (!h) return -1;
        if (h->child_reaped) {
            if (exit_status) *exit_status = h->exit_status;
            return 1;
        }
        int status = 0;
        pid_t r = waitpid(h->child_pid, &status, WNOHANG);
        if (r == 0) return 0;
        if (r > 0) {
            h->child_reaped = true;
            h->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            if (exit_status) *exit_status = h->exit_status;
            return 1;
        }
        return -1;
#else
        (void)pty;
        (void)exit_status;
        return -1;
#endif
    }

    int resize(pty_handle_t pty, int cols, int rows)
    {
#if defined(CP0_HAS_POSIX_PTY)
        std::lock_guard<std::mutex> lock(handles_mutex_);
        cp0_pty_handle *h = resolve(pty);
        if (!h) return -1;
        struct winsize ws = {};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);
        return ioctl(h->master_fd, TIOCSWINSZ, &ws);
#else
        (void)pty;
        (void)cols;
        (void)rows;
        return -1;
#endif
    }

    bool close(pty_handle_t pty)
    {
#if defined(CP0_HAS_POSIX_PTY)
        cp0_pty_handle *handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(handles_mutex_);
            auto it = handles_.find(reinterpret_cast<uintptr_t>(pty));
            if (it == handles_.end()) return false;
            handle = it->second;
            handles_.erase(it);
        }
        cleanup(handle);
        return true;
#else
        (void)pty;
        return false;
#endif
    }

    void shutdown() noexcept
    {
#if defined(CP0_HAS_POSIX_PTY)
        std::unordered_map<uintptr_t, cp0_pty_handle *> handles;
        try {
            std::lock_guard<std::mutex> lock(handles_mutex_);
            accepting_ = false;
            handles.swap(handles_);
        } catch (...) {
            return;
        }
        for (const auto &entry : handles) cleanup(entry.second);
#endif
    }

    void api_call(arg_t arg, callback_t callback)
    {
        cp0::pty::ParseResult parsed = cp0::pty::parse_request(arg);
        if (!parsed.ok) return report(callback, -1, parsed.error);
        const cp0::pty::Request &request = parsed.request;
        switch (request.command) {
        case cp0::pty::Command::open: api_open(request, callback); break;
        case cp0::pty::Command::read: api_read(request, callback); break;
        case cp0::pty::Command::write: api_write(request, callback); break;
        case cp0::pty::Command::resize: {
            const int result = resize(to_handle(request.handle), request.columns, request.rows);
            report(callback, result, result < 0 ? "resize pty failed\n" : "");
            break;
        }
        case cp0::pty::Command::check_child: api_check_child(request, callback); break;
        case cp0::pty::Command::close: {
            const bool closed = close(to_handle(request.handle));
            report(callback, closed ? 0 : -1, closed ? "" : "close pty failed\n");
            break;
        }
        }
    }

private:
    static void cleanup(cp0_pty_handle *handle) noexcept
    {
#if defined(CP0_HAS_POSIX_PTY)
        if (!handle) return;
        if (!handle->child_reaped) {
            (void)kill(handle->child_pid, SIGKILL);
            while (waitpid(handle->child_pid, NULL, 0) < 0 && errno == EINTR) {
            }
        }
        if (handle->master_fd >= 0) (void)::close(handle->master_fd);
        std::free(handle);
#else
        (void)handle;
#endif
    }

    static void report(callback_t callback, int code, const std::string &data)
    {
        if (callback) callback(code, data);
    }

    cp0_pty_handle *resolve(pty_handle_t pty)
    {
        auto it = handles_.find(reinterpret_cast<uintptr_t>(pty));
        return it == handles_.end() ? nullptr : it->second;
    }

    static pty_handle_t to_handle(std::uint64_t value)
    {
        if (value > static_cast<std::uint64_t>(UINTPTR_MAX)) return nullptr;
        return reinterpret_cast<pty_handle_t>(static_cast<uintptr_t>(value));
    }

    static std::string config_get_str(const char *key, const char *default_val)
    {
        std::string value = default_val ? default_val : "";
        cp0_signal_config_api({"GetStr", key ? std::string(key) : std::string(), value},
                              [&](int code, std::string data) {
                                  if (code == 0) value = std::move(data);
                              });
        return value;
    }

    static void drop_root_user(const std::string &configured_user)
    {
#if defined(__linux__)
        if (getuid() != 0) return;

        const char *username = configured_user.empty() ? NULL : configured_user.c_str();
        if (!username) {
            struct passwd *p = NULL;
            setpwent();
            while ((p = getpwent()) != NULL) {
                if (p->pw_uid >= 1000 && p->pw_uid < 65534 &&
                    p->pw_shell && p->pw_shell[0] &&
                    !std::strstr(p->pw_shell, "nologin") &&
                    !std::strstr(p->pw_shell, "/bin/false")) {
                    username = p->pw_name;
                    break;
                }
            }
            endpwent();
        }
        if (!username) username = "pi";

        struct passwd *pw = getpwnam(username);
        if (pw && std::strcmp(username, "root") != 0) {
            const int initgroups_result = initgroups(pw->pw_name, pw->pw_gid);
            const int setgid_result = setgid(pw->pw_gid);
            const int setuid_result = setuid(pw->pw_uid);
            const int home_result = setenv("HOME", pw->pw_dir, 1);
            const int user_result = setenv("USER", pw->pw_name, 1);
            const int logname_result = setenv("LOGNAME", pw->pw_name, 1);
            const int shell_result =
                setenv("SHELL", pw->pw_shell[0] ? pw->pw_shell : "/bin/bash", 1);
            const int chdir_result = chdir(pw->pw_dir);
            (void)initgroups_result;
            (void)setgid_result;
            (void)setuid_result;
            (void)home_result;
            (void)user_result;
            (void)logname_result;
            (void)shell_result;
            (void)chdir_result;
        }
#else
        (void)configured_user;
#endif
    }

    void api_open(const cp0::pty::Request &request, callback_t callback)
    {
        std::vector<const char *> argv;
        if (!request.argv.empty()) {
            for (const std::string &item : request.argv) argv.push_back(item.c_str());
            argv.push_back(NULL);
        }

        pty_handle_t pty = open(request.executable.c_str(), argv.empty() ? NULL : argv.data(),
                                request.columns, request.rows);
        if (!pty) {
            report(callback, -1, "open pty failed\n");
            return;
        }
        report(callback, 0, cp0::pty::encode_handle(reinterpret_cast<uintptr_t>(pty)));
    }

    void api_read(const cp0::pty::Request &request, callback_t callback)
    {
        pty_handle_t pty = to_handle(request.handle);
        std::string out(request.max_read, '\0');
        int n = read(pty, &out[0], out.size());
        if (n < 0) {
            report(callback, -1, "read pty failed\n");
            return;
        }
        out.resize(static_cast<size_t>(n));
        report(callback, n, out);
    }

    void api_write(const cp0::pty::Request &request, callback_t callback)
    {
        int n = write(to_handle(request.handle), request.data.data(), request.data.size());
        report(callback, n, n < 0 ? "write pty failed\n" : "");
    }

    void api_check_child(const cp0::pty::Request &request, callback_t callback)
    {
        int status = 0;
        int ret = check_child(to_handle(request.handle), &status);
        report(callback, ret, std::to_string(status));
    }

    std::unordered_map<uintptr_t, cp0_pty_handle *> handles_;
    std::mutex handles_mutex_;
#if defined(CP0_HAS_POSIX_PTY)
    bool accepting_ = true;
#endif
};

std::mutex g_pty_mutex;
std::shared_ptr<PtySystem> g_pty;
cp0::SignalRegistration<decltype(cp0_signal_pty_api)> g_pty_registration;

} // namespace

extern "C" void init_pty(void)
{
    try {
        std::lock_guard<std::mutex> lock(g_pty_mutex);
        if (g_pty) return;
        auto pty = std::make_shared<PtySystem>();
        if (!g_pty_registration.replace(
                cp0_signal_pty_api,
                [pty](std::list<std::string> arg,
                      std::function<void(int, std::string)> callback) {
                    pty->api_call(std::move(arg), std::move(callback));
                }))
            return;
        g_pty = std::move(pty);
    } catch (...) {
    }
}

extern "C" void deinit_pty(void) noexcept
{
    try {
        std::shared_ptr<PtySystem> pty;
        {
            std::lock_guard<std::mutex> lock(g_pty_mutex);
            try {
                g_pty_registration.reset();
            } catch (...) {
            }
            pty = std::move(g_pty);
        }
        if (pty) pty->shutdown();
    } catch (...) {
    }
}

#undef CP0_HAS_POSIX_PTY
