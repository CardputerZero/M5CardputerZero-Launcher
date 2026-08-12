#include "cp0_external_app_runner.hpp"

#include "cp0_esc_exit_policy.hpp"
#include "cp0_esc_state.h"
#include "../cp0_external_process_group.hpp"
#include "cp0_process_commands.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

extern "C" void __attribute__((weak)) keyboard_pause(void) {}
extern "C" void __attribute__((weak)) keyboard_resume(void) {}
extern "C" void __attribute__((weak)) ui_external_esc_hint(int visible) { (void)visible; }

namespace cp0_external_app_runner {
namespace {

const char *keyboard_device()
{
    const char *configured = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE");
    return configured ? configured : "/dev/input/by-path/platform-3f804000.i2c-event";
}

std::uint64_t monotonic_ms()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

int run(const char *command, bool keep_root)
{
#if defined(_WIN32)
    (void)command;
    (void)keep_root;
    return -1;
#else
    keyboard_pause();
    const bool subreaper = cp0_process_group::enable_subreaper();

    const int keyboard_fd = open(keyboard_device(), O_RDONLY | O_NONBLOCK);
    if (keyboard_fd < 0) {
        std::perror("[cp0] open evdev");
        keyboard_resume();
        return -1;
    }
    std::printf("[cp0] Opened evdev %s (no EVIOCGRAB; shared with child)\n", keyboard_device());
    std::fflush(stdout);

    const pid_t pid = fork();
    if (pid < 0) {
        close(keyboard_fd);
        keyboard_resume();
        return -1;
    }
    if (pid == 0) {
        close(keyboard_fd);
        setpgid(0, 0);
        if (keep_root)
            execlp("/bin/sh", "sh", "-c", command, static_cast<char *>(nullptr));
        else
            cp0_process_commands::exec_shell_as_configured_user(command);
        _exit(127);
    }

    setpgid(pid, pid);
    std::fprintf(stderr,
                 "[process] external app leader=%d pgid=%d subreaper=%d\n",
                 static_cast<int>(pid),
                 static_cast<int>(pid),
                 subreaper ? 1 : 0);

    cp0_esc_exit_policy::StateMachine esc_policy;
    bool leader_reaped = false;
    int status = 0;

    while (true) {
        cp0_process_group::reap_available(pid, pid, status, leader_reaped);
        if (!cp0_process_group::exists(pid)) break;

        struct input_event event;
        while (read(keyboard_fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event))) {
            if (event.type == EV_KEY && event.code == KEY_ESC) {
                if (event.value == 1)
                    cp0_esc_state_write(1);
                else if (event.value == 0)
                    cp0_esc_state_write(0);
            }
        }

        const bool esc_now = cp0_esc_state_read() != 0;
        const auto decision = esc_policy.update(monotonic_ms(), esc_now);
        if (decision.show_hint) ui_external_esc_hint(1);
        if (decision.hide_hint) ui_external_esc_hint(0);
        if (decision.send_terminate) {
            std::fprintf(stderr, "[process] ESC timeout: SIGTERM pgid=%d\n", static_cast<int>(pid));
            killpg(pid, SIGTERM);
        }
        if (decision.send_kill) {
            std::fprintf(stderr, "[process] grace timeout: SIGKILL pgid=%d\n", static_cast<int>(pid));
            killpg(pid, SIGKILL);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (esc_policy.finish().hide_hint) ui_external_esc_hint(0);
    cp0_process_group::reap_available(pid, pid, status, leader_reaped);
    std::fprintf(stderr,
                 "[process] external app group drained pgid=%d leader_reaped=%d\n",
                 static_cast<int>(pid),
                 leader_reaped ? 1 : 0);
    close(keyboard_fd);
    keyboard_resume();
    cp0_esc_state_reset();
    std::printf("[cp0] Returned to launcher\n");
    std::fflush(stdout);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

} // namespace cp0_external_app_runner
