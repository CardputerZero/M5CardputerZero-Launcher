#include "command_runner.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace launch_wizard {
namespace {

using Clock = std::chrono::steady_clock;

void close_fd(int &fd)
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool make_pipe(int fds[2])
{
#ifdef O_CLOEXEC
    if (pipe2(fds, O_CLOEXEC) == 0)
        return true;
    if (errno != ENOSYS)
        return false;
#endif
    if (pipe(fds) != 0)
        return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
}

void set_nonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void append_bounded(std::string &output, const char *data, size_t size,
                    size_t limit, bool &truncated)
{
    if (output.size() < limit) {
        const size_t count = std::min(size, limit - output.size());
        output.append(data, count);
        truncated |= count != size;
    } else {
        truncated = true;
    }
}

void signal_group(pid_t pid, int signal_number)
{
    if (pid > 0 && kill(-pid, signal_number) != 0 && errno == ESRCH)
        kill(pid, signal_number);
}

ssize_t write_without_sigpipe(int fd, const void *data, size_t size)
{
    sigset_t blocked;
    sigset_t previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    const ssize_t result = write(fd, data, size);
    if (result < 0 && errno == EPIPE && !sigismember(&previous, SIGPIPE)) {
        timespec no_wait{};
        while (sigtimedwait(&blocked, nullptr, &no_wait) < 0 && errno == EINTR) {}
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    return result;
}

}  // namespace

CommandResult run_command_process(const std::vector<std::string> &args,
                                  const std::string *stdin_text,
                                  const CommandOptions &options)
{
    CommandResult result;
    if (args.empty()) {
        result.code = 127;
        result.output = "No command was provided";
        return result;
    }

    int out_pipe[2] = {-1, -1};
    int in_pipe[2] = {-1, -1};
    if (!make_pipe(out_pipe) || (stdin_text && !make_pipe(in_pipe))) {
        const int saved_errno = errno;
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
        close_fd(in_pipe[0]);
        close_fd(in_pipe[1]);
        result.code = 127;
        result.output = strerror(saved_errno);
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        const int saved_errno = errno;
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
        close_fd(in_pipe[0]);
        close_fd(in_pipe[1]);
        result.code = 127;
        result.output = strerror(saved_errno);
        return result;
    }
    if (pid == 0) {
        setpgid(0, 0);
        if (stdin_text) {
            dup2(in_pipe[0], STDIN_FILENO);
        } else {
            const int null_fd = open("/dev/null", O_RDONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDIN_FILENO);
                close(null_fd);
            }
        }
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
        close_fd(in_pipe[0]);
        close_fd(in_pipe[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const std::string &arg : args)
            argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        const char *message = strerror(errno);
        write(STDERR_FILENO, message, strlen(message));
        _exit(127);
    }

    // Close child ends before any failure path and establish the group from
    // both sides, avoiding the fork/setpgid race.
    setpgid(pid, pid);
    close_fd(out_pipe[1]);
    close_fd(in_pipe[0]);
    set_nonblocking(out_pipe[0]);
    if (stdin_text)
        set_nonblocking(in_pipe[1]);

    size_t input_offset = 0;
    bool output_truncated = false;
    bool child_reaped = false;
    bool terminating = false;
    bool killed = false;
    int status = 0;
    const auto started = Clock::now();
    auto termination_started = Clock::time_point::max();
    auto child_exited_at = Clock::time_point::max();

    while (!child_reaped || out_pipe[0] >= 0) {
        const auto now = Clock::now();
        if (!terminating) {
            bool cancelled = false;
            if (options.cancelled) {
                try {
                    cancelled = options.cancelled();
                } catch (...) {
                    cancelled = true;
                }
            }
            if (cancelled || (options.timeout.count() >= 0 && now - started >= options.timeout)) {
                result.was_cancelled = cancelled;
                result.timed_out = !cancelled;
                terminating = true;
                termination_started = now;
                close_fd(in_pipe[1]);
                signal_group(pid, SIGTERM);
            }
        } else if (!killed && now - termination_started >= options.terminate_grace) {
            signal_group(pid, SIGKILL);
            killed = true;
        }

        if (!child_reaped) {
            const pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                child_reaped = true;
                child_exited_at = now;
                close_fd(in_pipe[1]);
            } else if (waited < 0 && errno != EINTR) {
                child_reaped = true;
                status = -1;
                child_exited_at = now;
                close_fd(in_pipe[1]);
            }
        }

        struct pollfd fds[2];
        nfds_t count = 0;
        int output_index = -1;
        int input_index = -1;
        if (out_pipe[0] >= 0) {
            output_index = static_cast<int>(count);
            fds[count++] = {out_pipe[0], POLLIN | POLLHUP, 0};
        }
        if (in_pipe[1] >= 0) {
            if (!stdin_text || input_offset >= stdin_text->size()) {
                close_fd(in_pipe[1]);
            } else {
                input_index = static_cast<int>(count);
                fds[count++] = {in_pipe[1], POLLOUT | POLLHUP, 0};
            }
        }

        const int poll_result = poll(fds, count, 20);
        if (poll_result > 0 && output_index >= 0 &&
            (fds[output_index].revents & (POLLIN | POLLHUP | POLLERR))) {
            char buffer[1024];
            while (true) {
                const ssize_t bytes = read(out_pipe[0], buffer, sizeof(buffer));
                if (bytes > 0) {
                    append_bounded(result.output, buffer, static_cast<size_t>(bytes),
                                   options.max_output_bytes, output_truncated);
                } else if (bytes == 0) {
                    close_fd(out_pipe[0]);
                    break;
                } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                    close_fd(out_pipe[0]);
                    break;
                } else if (errno != EINTR) {
                    break;
                }
            }
        }
        if (poll_result > 0 && input_index >= 0 &&
            (fds[input_index].revents & (POLLOUT | POLLHUP | POLLERR))) {
            if (fds[input_index].revents & POLLOUT) {
                const char *data = stdin_text->data() + input_offset;
                const size_t left = stdin_text->size() - input_offset;
                const ssize_t bytes = write_without_sigpipe(in_pipe[1], data, left);
                if (bytes > 0)
                    input_offset += static_cast<size_t>(bytes);
                else if (bytes < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                    close_fd(in_pipe[1]);
                if (input_offset == stdin_text->size())
                    close_fd(in_pipe[1]);
            } else {
                close_fd(in_pipe[1]);
            }
        }

        // A grandchild may inherit the output pipe after the command exits.
        // Give buffered output a short drain window, then clean up the group.
        if (child_reaped && out_pipe[0] >= 0 && now - child_exited_at >= std::chrono::milliseconds(100)) {
            signal_group(pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            signal_group(pid, SIGKILL);
            close_fd(out_pipe[0]);
        }
    }

    close_fd(in_pipe[1]);
    close_fd(out_pipe[0]);
    if (terminating && !killed) {
        const auto kill_at = termination_started + options.terminate_grace;
        if (Clock::now() < kill_at)
            std::this_thread::sleep_until(kill_at);
        signal_group(pid, SIGKILL);
    }
    if (!child_reaped) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }

    if (result.was_cancelled) {
        result.code = 125;
        result.output = "Configuration was cancelled. You can retry when ready.";
    } else if (result.timed_out) {
        result.code = 124;
        result.output = "This configuration step timed out. Check the service or connection, then retry.";
    } else if (status < 0) {
        result.code = 127;
        if (result.output.empty())
            result.output = "Could not collect the command result";
    } else if (WIFEXITED(status)) {
        result.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.code = 128 + WTERMSIG(status);
    } else {
        result.code = 127;
    }

    while (!result.output.empty() &&
           (result.output.back() == '\n' || result.output.back() == '\r'))
        result.output.pop_back();
    if (output_truncated && !result.timed_out && !result.was_cancelled)
        result.output += "\n[output truncated]";
    return result;
}

}  // namespace launch_wizard
