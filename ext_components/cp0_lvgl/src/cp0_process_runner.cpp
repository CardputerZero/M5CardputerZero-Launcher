#include "cp0_process_runner.hpp"
#include "cp0_async_testable_utils.hpp"
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#if !defined(_WIN32)
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

#if !defined(_WIN32)
extern char **environ;
#endif
namespace cp0_runner {
Result run(std::vector<std::string> argv, const std::string *input, Output output,
           const std::atomic<bool> *cancel, int timeout_ms, std::size_t capacity,
           std::uint32_t portable_uid, std::uint32_t portable_gid,
           const std::string &user_name,
           const std::string &home, const std::string &shell,
           InputConsumed input_consumed)
{
#if defined(_WIN32)
    (void)argv; (void)input; (void)output; (void)cancel; (void)timeout_ms;
    (void)capacity; (void)portable_uid; (void)portable_gid; (void)user_name;
    (void)home; (void)shell; (void)input_consumed;
    return {-ENOTSUP, {}};
#else
    const uid_t uid = static_cast<uid_t>(portable_uid);
    const gid_t gid = static_cast<gid_t>(portable_gid);
    Result result;
    if (argv.empty()) { result.exit_code = -EINVAL; return result; }
    std::string executable = argv[0];
    if (executable.find('/') == std::string::npos) {
        const char *path = std::getenv("PATH");
        std::string paths = path ? path : "/usr/bin:/bin";
        executable.clear();
        for (std::size_t begin = 0; begin <= paths.size();) {
            std::size_t end = paths.find(':', begin);
            std::string directory = paths.substr(begin, end - begin);
            if (directory.empty()) directory = ".";
            std::string candidate = directory + "/" + argv[0];
            if (access(candidate.c_str(), X_OK) == 0) { executable = candidate; break; }
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }
    if (executable.empty()) { result.exit_code = -ENOENT; return result; }
    std::vector<char *> raw;
    for (auto &arg : argv) raw.push_back(arg.data());
    raw.push_back(nullptr);
    std::vector<std::string> environment;
    for (char **entry = environ; entry && *entry; ++entry) {
        std::string value(*entry);
        const auto equal = value.find('=');
        const std::string key = value.substr(0, equal + 1);
        const bool identity_key = key == "HOME=" || key == "USER=" ||
            key == "LOGNAME=" || key == "SHELL=";
        if (key != "LC_ALL=" && (!identity_key || user_name.empty()))
            environment.push_back(std::move(value));
    }
    if (!user_name.empty()) {
        environment.push_back("HOME=" + home);
        environment.push_back("USER=" + user_name);
        environment.push_back("LOGNAME=" + user_name);
        environment.push_back("SHELL=" + shell);
    }
    environment.push_back("LC_ALL=C");
    std::vector<char *> raw_environment;
    for (auto &entry : environment) raw_environment.push_back(entry.data());
    raw_environment.push_back(nullptr);
    std::vector<gid_t> groups;
    if (uid != static_cast<uid_t>(-1) && !user_name.empty()) {
        int count = 0;
        getgrouplist(user_name.c_str(), gid, nullptr, &count);
        if (count > 0) {
#if defined(__APPLE__)
            std::vector<int> native_groups(count);
            if (getgrouplist(user_name.c_str(), static_cast<int>(gid),
                             native_groups.data(), &count) < 0) {
                result.exit_code = -EINVAL; return result;
            }
            native_groups.resize(count);
            groups.assign(native_groups.begin(), native_groups.end());
#else
            groups.resize(count);
            if (getgrouplist(user_name.c_str(), gid, groups.data(), &count) < 0) {
                result.exit_code = -EINVAL; return result;
            }
            groups.resize(count);
#endif
        }
    }
    int in[2] = {-1, -1}, out[2] = {-1, -1};
    if (pipe(out)) { result.exit_code = -errno; return result; }
    if (input && pipe(in)) { result.exit_code = -errno; close(out[0]); close(out[1]); return result; }
    char *const *raw_ptr = raw.data();
    char *const *env_ptr = raw_environment.data();
    const char *executable_ptr = executable.c_str();
    const gid_t *groups_ptr = groups.data();
    const size_t groups_count = groups.size();
    pid_t pid = fork();
    if (pid < 0) { result.exit_code = -errno; close(out[0]); close(out[1]); if (input) { close(in[0]); close(in[1]); } return result; }
    if (pid == 0) {
        setpgid(0, 0);
        if (input) { close(in[1]); dup2(in[0], STDIN_FILENO); if (in[0] != STDIN_FILENO) close(in[0]); }
        else { int n = open("/dev/null", O_RDONLY); if (n >= 0) { dup2(n, STDIN_FILENO); if (n != STDIN_FILENO) close(n); } }
        close(out[0]); dup2(out[1], STDOUT_FILENO); dup2(out[1], STDERR_FILENO);
        if (out[1] != STDOUT_FILENO && out[1] != STDERR_FILENO) close(out[1]);
        if (uid != static_cast<uid_t>(-1) && geteuid() == 0 &&
            ((groups_count && setgroups(groups_count, groups_ptr) != 0) ||
             setgid(gid) != 0 || setuid(uid) != 0)) _exit(126);
        execve(executable_ptr, raw_ptr, env_ptr);
        _exit(127);
    }
    close(out[1]);
    bool input_notified = false;
    auto finish_input = [&]() {
        if (input && in[1] >= 0) {
            close(in[1]);
            in[1] = -1;
        }
        if (input && !input_notified) {
            input_notified = true;
            if (input_consumed) input_consumed();
        }
    };
    sigset_t set{}, old{}, pending{};
    bool sigpipe_blocked = false, sigpipe_was_pending = false;
    if (input) {
        close(in[0]);
        sigemptyset(&set); sigaddset(&set, SIGPIPE);
        if (pthread_sigmask(SIG_BLOCK, &set, &old) == 0) {
            sigpipe_blocked = true;
            if (sigpending(&pending) == 0) sigpipe_was_pending = sigismember(&pending, SIGPIPE);
        }
        if (!sigpipe_blocked) { close(in[1]); in[1] = -1; }
        if (in[1] >= 0) {
            int flags = fcntl(in[1], F_GETFL, 0);
            if (flags >= 0) fcntl(in[1], F_SETFL, flags | O_NONBLOCK);
        }
    }
    setpgid(pid, pid);
    fcntl(out[0], F_SETFL, fcntl(out[0], F_GETFL, 0) | O_NONBLOCK);
    auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : INT_MAX);
    bool terminated = false, forced = false, reaped = false, output_eof = false;
    auto force_deadline = std::chrono::steady_clock::time_point::max();
    int output_error = 0;
    size_t input_offset = 0;
    char buffer[4096];
    for (;;) {
        const bool reaped_before_drain = reaped;
        bool cancelled = cancel && cancel->load();
        bool timed_out = timeout_ms > 0 && std::chrono::steady_clock::now() >= deadline;
        if ((cancelled || timed_out) && !terminated && !reaped) {
            kill(-pid, SIGTERM);
            result.exit_code = cancelled ? -ECANCELED : -ETIMEDOUT;
            terminated = true;
            force_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            finish_input();
        }
        if (terminated && !forced) {
            siginfo_t info{};
            bool leader_exited = waitid(P_PID, pid, &info, WEXITED | WNOHANG | WNOWAIT) == 0 &&
                                 info.si_pid == pid;
            if (leader_exited || std::chrono::steady_clock::now() >= force_deadline) {
                kill(-pid, SIGKILL);
                forced = true;
            }
        }
        bool output_drained = false;
        size_t drained_bytes = 0;
        for (;;) {
            ssize_t count = read(out[0], buffer, sizeof(buffer));
            if (count > 0) {
                cp0_testable::append_tail(result.output, buffer, count, capacity);
                if (output) output(buffer, count);
                drained_bytes += static_cast<size_t>(count);
                if (drained_bytes >= 64 * 1024) break;
                continue;
            }
            if (count == 0) output_eof = true;
            if (count < 0 && errno == EINTR) continue;
            if (count == 0 || (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
                output_drained = true;
            else if (count < 0) {
                output_error = -errno;
                close(out[0]); out[0] = -1;
                output_eof = true;
            }
            break;
        }
        if (input && in[1] >= 0 && input_offset < input->size()) {
            ssize_t count = write(in[1], input->data() + input_offset, input->size() - input_offset);
            if (count > 0) input_offset += static_cast<size_t>(count);
            else if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                finish_input();
            }
        }
        if (input && in[1] >= 0 && input_offset == input->size()) finish_input();
        // Keep the leader unreaped during the grace period. Its PID then cannot
        // be reused as another process group's ID before the forced group kill.
        if (!reaped && (!terminated || forced)) {
            int status = 0; pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                reaped = true;
                if (!terminated) result.exit_code = cp0_testable::prefer_io_error(
                    output_error, WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
            } else if (waited < 0 && errno != EINTR) { reaped = true; if (!terminated) result.exit_code = -errno; }
        }
        if (reaped && (output_eof || (reaped_before_drain && output_drained))) break;
        pollfd fds[2]{{out[0], POLLIN, 0}, {input && in[1] >= 0 ? in[1] : -1, POLLOUT, 0}};
        poll(fds, 2, 20);
    }
    finish_input();
    if (sigpipe_blocked) {
        if (!sigpipe_was_pending && sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE)) {
#if defined(__APPLE__)
            int consumed_signal = 0;
            sigwait(&set, &consumed_signal);
#else
            timespec zero{0, 0}; sigtimedwait(&set, nullptr, &zero);
#endif
        }
        pthread_sigmask(SIG_SETMASK, &old, nullptr);
    }
    if (out[0] >= 0) close(out[0]);
    return result;
#endif
}
}
