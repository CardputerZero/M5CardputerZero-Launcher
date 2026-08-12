#include "../main/ui/esc_ui_watchdog.h"

#include "keyboard_input.h"
#include "cp0_esc_state.h"

#include <cassert>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    const auto start = std::chrono::steady_clock::now();
    const pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        EscUiWatchdog watchdog;
        watchdog.start();
        watchdog.arm();
        cp0_esc_state_write(KBD_KEY_PRESSED);
        for (;;) pause();
    }

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 75);
    assert(elapsed >= std::chrono::milliseconds(3500));
    assert(elapsed < std::chrono::milliseconds(4000));
}
