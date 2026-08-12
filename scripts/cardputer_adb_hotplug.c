#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static bool numeric_name(const char *name)
{
    if (!name || !*name) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (!isdigit(*p)) return false;
    }
    return true;
}

static bool read_first_line(const char *path, char *buffer, size_t size)
{
    if (!path || !buffer || size < 2) return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const ssize_t count = read(fd, buffer, size - 1);
    close(fd);
    if (count <= 0) return false;
    buffer[count] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return true;
}

static pid_t find_adbd(void)
{
    DIR *proc = opendir("/proc");
    if (!proc) return 0;
    pid_t result = 0;
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (!numeric_name(entry->d_name)) continue;
        char path[PATH_MAX];
        char name[32];
        if (snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name) >=
            (int)sizeof(path))
            continue;
        if (read_first_line(path, name, sizeof(name)) && strcmp(name, "adbd") == 0) {
            result = (pid_t)strtol(entry->d_name, NULL, 10);
            break;
        }
    }
    closedir(proc);
    return result;
}

static bool adbd_has_functionfs(void)
{
    const pid_t pid = find_adbd();
    if (pid <= 0) return false;

    char directory[64];
    if (snprintf(directory, sizeof(directory), "/proc/%ld/fd", (long)pid) >=
        (int)sizeof(directory))
        return false;
    DIR *fds = opendir(directory);
    if (!fds) return false;

    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(fds)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX];
        char target[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >=
            (int)sizeof(path))
            continue;
        const ssize_t count = readlink(path, target, sizeof(target) - 1);
        if (count <= 0) continue;
        target[count] = '\0';
        if (strcmp(target, "/dev/usb-ffs/adb/ep0") == 0) {
            found = true;
            break;
        }
    }
    closedir(fds);
    return found;
}

static int find_udc_state(const char *root, char *path, size_t size)
{
    char directory[PATH_MAX];
    if (snprintf(directory, sizeof(directory), "%s/sys/class/udc", root) >=
        (int)sizeof(directory))
        return -1;
    DIR *udcs = opendir(directory);
    if (!udcs) return -1;

    int fd = -1;
    struct dirent *entry;
    while ((entry = readdir(udcs)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (snprintf(path, size, "%s/%s/state", directory, entry->d_name) >= (int)size)
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) break;
    }
    closedir(udcs);
    return fd;
}

static bool read_udc_state(int fd, char *state, size_t size)
{
    if (!state || size < 2) return false;
    if (lseek(fd, 0, SEEK_SET) < 0) return false;
    const ssize_t count = read(fd, state, size - 1);
    if (count <= 0) return false;
    state[count] = '\0';
    state[strcspn(state, "\r\n")] = '\0';
    return true;
}

static bool state_needs_transport(const char *state)
{
    return state && (strcmp(state, "configured") == 0 || strcmp(state, "suspended") == 0);
}

static int restart_adbd(void)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        execlp("systemctl", "systemctl", "restart", "adbd.service", (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static void sleep_milliseconds(long milliseconds)
{
    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L,
    };
    while (!stop_requested && nanosleep(&delay, &delay) < 0 && errno == EINTR) {
    }
}

int main(void)
{
    const char *root = "";
    int timeout_ms = 5000;
    if (geteuid() != 0) {
        const char *test_root = getenv("CARDPUTER_ADB_TEST_ROOT");
        const char *test_timeout = getenv("CARDPUTER_ADB_HOTPLUG_TIMEOUT_MS");
        if (test_root && *test_root) root = test_root;
        if (test_timeout && atoi(test_timeout) >= 10) timeout_ms = atoi(test_timeout);
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    int state_fd = -1;
    char state_path[PATH_MAX];
    time_t last_restart = 0;
    const time_t started = time(NULL);
    while (!stop_requested) {
        if (state_fd < 0) {
            state_fd = find_udc_state(root, state_path, sizeof(state_path));
            if (state_fd < 0) {
                sleep_milliseconds(1000);
                continue;
            }
            char initial_state[64];
            (void)read_udc_state(state_fd, initial_state, sizeof(initial_state));
        }

        struct pollfd descriptor = {
            .fd = state_fd,
            .events = POLLPRI | POLLERR,
            .revents = 0,
        };
        const int ready = poll(&descriptor, 1, timeout_ms);
        if (ready < 0 && errno != EINTR) {
            close(state_fd);
            state_fd = -1;
            continue;
        }
        if (stop_requested) break;

        char state[64];
        if (!read_udc_state(state_fd, state, sizeof(state))) {
            close(state_fd);
            state_fd = -1;
            continue;
        }
        if (!state_needs_transport(state)) continue;
        const bool state_changed = ready > 0 &&
            (descriptor.revents & (POLLPRI | POLLERR)) != 0;
        const bool transport_missing = !adbd_has_functionfs();
        if (!state_changed && !transport_missing) continue;

        const time_t now = time(NULL);
        if (state_changed && !transport_missing && started != (time_t)-1 &&
            now != (time_t)-1 && now - started <= 5)
            continue;
        if (now != (time_t)-1 && last_restart != 0 && now - last_restart <= 5) continue;
        last_restart = now;
        fprintf(stderr,
                "USB entered %s state%s; restarting adbd\n",
                state,
                transport_missing ? " without a FunctionFS transport" : "");
        if (restart_adbd() != 0) {
            fprintf(stderr, "failed to restart adbd.service\n");
        }
        close(state_fd);
        state_fd = -1;
        sleep_milliseconds(2000);
    }

    if (state_fd >= 0) close(state_fd);
    return 0;
}
