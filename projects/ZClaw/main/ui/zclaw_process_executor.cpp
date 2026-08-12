#include "zclaw_process_executor.h"

#include "zclaw_text.h"

#include <cerrno>
#include <chrono>

#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace zclaw {
namespace {

CommandResult cancelled_result()
{
    return {-1, "command cancelled"};
}

bool close_on_exec(int descriptor)
{
    const int flags = ::fcntl(descriptor, F_GETFD);
    return flags >= 0 && ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

}  // namespace

ProcessExecutor::~ProcessExecutor()
{
    shutdown();
}

CommandResult ProcessExecutor::run(const std::vector<std::string> &arguments)
{
    if (arguments.empty() || arguments.front().empty())
        return {-1, "missing command"};
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string &argument : arguments)
        argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);

    int output_pipe[2] = {-1, -1};
    pid_t process = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_)
            return cancelled_result();
        if (::pipe(output_pipe) != 0)
            return {-1, "failed to create command pipe"};
        if (!close_on_exec(output_pipe[0]) || !close_on_exec(output_pipe[1])) {
            ::close(output_pipe[0]);
            ::close(output_pipe[1]);
            return {-1, "failed to configure command pipe"};
        }
        process = ::fork();
        if (process > 0) {
            ::setpgid(process, process);
            try {
                active_processes_.insert(process);
            } catch (...) {
                ::kill(-process, SIGKILL);
                ::kill(process, SIGKILL);
                ::close(output_pipe[0]);
                ::close(output_pipe[1]);
                while (::waitpid(process, nullptr, 0) < 0 && errno == EINTR) {
                }
                throw;
            }
        }
    }

    if (process == 0) {
        ::setpgid(0, 0);
        ::close(output_pipe[0]);
        ::dup2(output_pipe[1], STDOUT_FILENO);
        ::dup2(output_pipe[1], STDERR_FILENO);
        ::close(output_pipe[1]);
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }

    ::close(output_pipe[1]);
    if (process < 0) {
        ::close(output_pipe[0]);
        return {-1, "failed to start command"};
    }

    const pid_t active_process = process;
    bool reaped = false;
    CommandResult result;
    try {
        bool read_failed = false;
        char buffer[256];
        while (true) {
            const ssize_t bytes = ::read(output_pipe[0], buffer, sizeof(buffer));
            if (bytes > 0) {
                result.output.append(buffer, static_cast<std::size_t>(bytes));
                continue;
            }
            if (bytes < 0 && errno == EINTR)
                continue;
            read_failed = bytes < 0;
            break;
        }
        ::close(output_pipe[0]);
        output_pipe[0] = -1;

        if (read_failed) {
            ::kill(-process, SIGKILL);
            ::kill(process, SIGKILL);
        }

        int status = -1;
        while (::waitpid(process, &status, 0) < 0 && errno == EINTR) {
        }
        reaped = true;
        result.status = status;
        result.output = trim_ascii_whitespace(result.output);
    } catch (...) {
        if (output_pipe[0] >= 0)
            ::close(output_pipe[0]);
        if (!reaped) {
            ::kill(-process, SIGKILL);
            ::kill(process, SIGKILL);
            while (::waitpid(process, nullptr, 0) < 0 && errno == EINTR) {
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_processes_.erase(active_process);
        }
        changed_.notify_all();
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_processes_.erase(active_process);
    }
    changed_.notify_all();
    return result;
}

CommandResult ProcessExecutor::run_with_secret_input(
    const std::vector<std::string> &arguments, const std::string &secret,
    int timeout_ms)
{
    if (arguments.empty() || arguments.front().empty() || timeout_ms <= 0)
        return {-1, "missing command"};

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string &argument : arguments)
        argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);

    int master = -1;
    int slave = -1;
    pid_t process = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_)
            return cancelled_result();
        master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
        if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0) {
            if (master >= 0)
                ::close(master);
            return {-1, "failed to create secure command terminal"};
        }
        const char *slave_name = ::ptsname(master);
        if (!slave_name) {
            ::close(master);
            return {-1, "failed to resolve secure command terminal"};
        }
        const std::string slave_path(slave_name);
        slave = ::open(slave_path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (slave < 0) {
            ::close(master);
            return {-1, "failed to open secure command terminal"};
        }
        struct termios settings {};
        if (::tcgetattr(slave, &settings) != 0) {
            ::close(slave);
            ::close(master);
            return {-1, "failed to configure secure command terminal"};
        }
        settings.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL));
        if (::tcsetattr(slave, TCSANOW, &settings) != 0) {
            ::close(slave);
            ::close(master);
            return {-1, "failed to configure secure command terminal"};
        }
        process = ::fork();
        if (process == 0) {
            ::setsid();
            ::ioctl(slave, TIOCSCTTY, 0);
            ::dup2(slave, STDIN_FILENO);
            ::dup2(slave, STDOUT_FILENO);
            ::dup2(slave, STDERR_FILENO);
            if (slave > STDERR_FILENO)
                ::close(slave);
            ::close(master);
            ::execvp(argv[0], argv.data());
            ::_exit(127);
        }
        if (process > 0) {
            ::close(slave);
            slave = -1;
            ::setpgid(process, process);
            try {
                active_processes_.insert(process);
            } catch (...) {
                ::kill(-process, SIGKILL);
                ::kill(process, SIGKILL);
                if (slave >= 0)
                    ::close(slave);
                ::close(master);
                while (::waitpid(process, nullptr, 0) < 0 && errno == EINTR) {
                }
                throw;
            }
        }
    }

    if (process < 0) {
        if (slave >= 0)
            ::close(slave);
        ::close(master);
        return {-1, "failed to start command"};
    }

    CommandResult result;
    bool reaped = false;
    try {
        std::string input = secret;
        input.push_back('\n');
        std::size_t written = 0;
        bool input_failed = false;
        bool timed_out = false;
        bool cancelled = false;
        bool eof = false;
        constexpr std::size_t kOutputLimit = 64 * 1024;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeout_ms);
        char buffer[256];
        while (!eof) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cancelled = shutdown_;
            }
            if (cancelled) break;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) { timed_out = true; break; }
            short events = POLLIN;
            if (written < input.size()) events |= POLLOUT;
            pollfd descriptor{master, events, 0};
            const int ready = ::poll(&descriptor, 1,
                                     static_cast<int>(std::min<int64_t>(remaining, 50)));
            if (ready < 0) {
                if (errno == EINTR) continue;
                input_failed = written < input.size();
                break;
            }
            if (ready == 0) continue;
            if ((descriptor.revents & POLLOUT) && written < input.size()) {
                const ssize_t count = ::write(master, input.data() + written,
                                              input.size() - written);
                if (count > 0) written += static_cast<std::size_t>(count);
                else if (count < 0 && errno != EINTR && errno != EAGAIN)
                    input_failed = true;
            }
            if (input_failed) break;
            if (descriptor.revents & (POLLIN | POLLHUP)) {
                while (true) {
                    const ssize_t bytes = ::read(master, buffer, sizeof(buffer));
                    if (bytes > 0) {
                        const std::size_t available = kOutputLimit - result.output.size();
                        result.output.append(buffer, std::min<std::size_t>(
                            static_cast<std::size_t>(bytes), available));
                        continue;
                    }
                    if (bytes == 0 || (bytes < 0 && errno == EIO)) eof = true;
                    else if (bytes < 0 && errno != EINTR && errno != EAGAIN)
                        input_failed = true;
                    break;
                }
            }
            if (descriptor.revents & (POLLERR | POLLNVAL)) input_failed = true;
            if (input_failed) break;
        }
        std::fill(input.begin(), input.end(), '\0');
        if (timed_out || cancelled || input_failed || written < input.size()) {
            ::kill(-process, SIGTERM);
            ::kill(process, SIGTERM);
            for (int attempt = 0; attempt < 10; ++attempt) {
                if (::waitpid(process, nullptr, WNOHANG) == process) {
                    reaped = true;
                    break;
                }
                ::usleep(10000);
            }
            if (!reaped) {
                ::kill(-process, SIGKILL);
                ::kill(process, SIGKILL);
            }
        }
        ::close(master);
        master = -1;

        int status = -1;
        if (!reaped) {
            while (::waitpid(process, &status, 0) < 0 && errno == EINTR) {}
            reaped = true;
        }
        if (cancelled) result = cancelled_result();
        else if (timed_out) result = {-1, "command timed out"};
        else if (input_failed || written < input.size())
            result = {-1, "failed to provide secure input"};
        else result.status = status;
        result.output = trim_ascii_whitespace(result.output);
    } catch (...) {
        if (master >= 0)
            ::close(master);
        if (!reaped) {
            ::kill(-process, SIGKILL);
            ::kill(process, SIGKILL);
            while (::waitpid(process, nullptr, 0) < 0 && errno == EINTR) {
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_processes_.erase(process);
        }
        changed_.notify_all();
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_processes_.erase(process);
    }
    changed_.notify_all();
    return result;
}

void ProcessExecutor::wait(unsigned int seconds) const
{
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_for(lock, std::chrono::seconds(seconds),
                      [this] { return shutdown_; });
}

void ProcessExecutor::shutdown() noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!shutdown_) {
        shutdown_ = true;
        for (pid_t process : active_processes_) {
            if (::kill(-process, SIGKILL) != 0)
                ::kill(process, SIGKILL);
        }
        changed_.notify_all();
    }
    changed_.wait(lock, [this] { return active_processes_.empty(); });
}

bool ProcessExecutor::shutdown_requested() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return shutdown_;
}

}  // namespace zclaw
