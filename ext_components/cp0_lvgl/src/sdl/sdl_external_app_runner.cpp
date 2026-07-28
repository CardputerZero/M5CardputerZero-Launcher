#include "sdl_external_app_runner.hpp"

#include "cp0_esc_exit_policy.hpp"
#include "cp0_esc_state.h"
#include "../cp0_external_process_group.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace sdl_external_app_runner {

extern "C" void __attribute__((weak)) ui_external_esc_hint(int visible) { (void)visible; }

namespace {
std::uint64_t monotonic_ms()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
} // namespace

int run(const char *command)
{
#if defined(_WIN32)
    (void)command;
    return -1;
#else
    const bool subreaper = cp0_process_group::enable_subreaper();
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setpgid(0, 0);
        execlp("/bin/sh", "sh", "-c", command, static_cast<char *>(nullptr));
        _exit(127);
    }

    setpgid(pid, pid);
    std::fprintf(stderr, "[process] external app leader=%d pgid=%d subreaper=%d\n",
                 static_cast<int>(pid), static_cast<int>(pid), subreaper ? 1 : 0);

    int status = 0;
    bool leader_reaped = false;
    cp0_esc_exit_policy::StateMachine esc_policy;
    while (true) {
        cp0_process_group::reap_available(pid, pid, status, leader_reaped);
        if (!cp0_process_group::exists(pid)) break;

        const auto decision = esc_policy.update(
            monotonic_ms(), cp0_esc_state_read() != 0);
        if (decision.show_hint) ui_external_esc_hint(1);
        if (decision.hide_hint) ui_external_esc_hint(0);
        if (decision.send_terminate) {
            std::fprintf(stderr, "[process] ESC timeout: SIGTERM pgid=%d\n",
                         static_cast<int>(pid));
            killpg(pid, SIGTERM);
        }
        if (decision.send_kill) {
            std::fprintf(stderr, "[process] grace timeout: SIGKILL pgid=%d\n",
                         static_cast<int>(pid));
            killpg(pid, SIGKILL);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (esc_policy.finish().hide_hint) ui_external_esc_hint(0);
    cp0_process_group::reap_available(pid, pid, status, leader_reaped);
    std::fprintf(stderr, "[process] external app group drained pgid=%d leader_reaped=%d\n",
                 static_cast<int>(pid), leader_reaped ? 1 : 0);
    cp0_esc_state_reset();
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

} // namespace sdl_external_app_runner
