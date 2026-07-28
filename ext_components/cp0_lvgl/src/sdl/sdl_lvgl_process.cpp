#include "cp0_lvgl_app.h"
#include "hal/hal_process.h"
#include "hal_lvgl_bsp.h"

#include "../cp0/cp0_desktop_exec_policy.hpp"
#include "../cp0/cp0_process_commands.hpp"
#include "../cp0/cp0_process_lifecycle.hpp"
#include "../cp0_app_internal_utils.h"
#include "../cp0_process_api_contract.hpp"
#include "../cp0_signal_registration.hpp"
#include "sdl_external_app_runner.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class ProcessSystem
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

    void api_call(arg_t args, callback_t callback)
    {
        try {
        const std::string command = args.empty() ? "" : args.front();
        if (command == "ExecBlocking") {
            bool keep_root = false;
            const auto *executable = cp0_process_api_contract::argument_at(args, 1);
            const auto *keep_root_text = cp0_process_api_contract::argument_at(args, 2);
            if (!cp0_process_api_contract::has_exact_arguments(args, 3) || !executable ||
                executable->empty() || !keep_root_text ||
                !cp0_process_api_contract::parse_bool(*keep_root_text, keep_root))
                return invalid(callback);
            (void)keep_root;
            report(callback, sdl_external_app_runner::run(executable->c_str()), "");
        } else if (command == "Spawn") {
            bool keep_root = false;
            const auto *executable = cp0_process_api_contract::argument_at(args, 1);
            const auto *flag = cp0_process_api_contract::argument_at(args, 2);
            if (!cp0_process_api_contract::has_exact_arguments(args, 3) || !executable ||
                executable->empty() || !flag ||
                !cp0_process_api_contract::parse_bool(*flag, keep_root))
                return invalid(callback);
            const cp0_pid_t pid = cp0_process_lifecycle::spawn(executable->c_str(), keep_root);
            report(callback, pid < 0 ? -1 : 0, std::to_string(pid));
        } else if (command == "Stop") {
            int pid = 0;
            const auto *text = cp0_process_api_contract::argument_at(args, 1);
            if (!cp0_process_api_contract::has_exact_arguments(args, 2) || !text ||
                !cp0_process_api_contract::parse_pid(*text, pid))
                return invalid(callback);
            cp0_process_lifecycle::stop(static_cast<cp0_pid_t>(pid));
            report(callback, 0, "");
        } else if (command == "CheckLock") {
            const auto *path = cp0_process_api_contract::argument_at(args, 1);
            if (!cp0_process_api_contract::has_exact_arguments(args, 2) || !path || path->empty())
                return invalid(callback);
            int holder_pid = 0;
            const int result = cp0_process_lifecycle::check_lock(path->c_str(), &holder_pid);
            report(callback, result, std::to_string(holder_pid));
        } else if (command == "Kill") {
            int pid = 0;
            int grace_ms = 0;
            const auto *pid_text = cp0_process_api_contract::argument_at(args, 1);
            const auto *grace_text = cp0_process_api_contract::argument_at(args, 2);
            if (!cp0_process_api_contract::has_exact_arguments(args, 3) || !pid_text ||
                !grace_text || !cp0_process_api_contract::parse_pid(*pid_text, pid) ||
                !cp0_process_api_contract::parse_grace_ms(*grace_text, grace_ms))
                return invalid(callback);
            cp0_process_lifecycle::kill(pid, grace_ms);
            report(callback, 0, "");
        } else if (command == "RunArgv") {
            bool background = false;
            const auto *flag = cp0_process_api_contract::argument_at(args, 1);
            if (!cp0_process_api_contract::has_at_least_arguments(args, 3) || !flag ||
                !cp0_process_api_contract::parse_bool(*flag, background))
                return invalid(callback);
            report(callback, cp0_process_commands::run_argv(
                                 cp0_process_api_contract::arguments_from(args, 2), background), "");
        } else if (command == "RunSudo") {
            if (!cp0_process_api_contract::has_at_least_arguments(args, 3))
                return invalid(callback);
            report(callback,
                   cp0_process_commands::run_sudo(
                       *cp0_process_api_contract::argument_at(args, 1),
                       cp0_process_api_contract::arguments_from(args, 2)),
                   "");
        } else if (command == "CaptureArgv") {
            if (!cp0_process_api_contract::has_at_least_arguments(args, 2))
                return invalid(callback);
            std::string output;
            const int result = cp0_process_commands::capture_argv(
                cp0_process_api_contract::arguments_from(args, 1), output);
            report(callback, result, output);
        } else if (command == "AdbStatus") {
            if (!cp0_process_api_contract::has_exact_arguments(args, 1)) return invalid(callback);
            report(callback, 0, "adbd=inactive\n");
        } else if (command == "DesktopExecIsSafe") {
            const auto *exec = cp0_process_api_contract::argument_at(args, 1);
            if (!cp0_process_api_contract::has_exact_arguments(args, 2) || !exec || exec->empty())
                return invalid(callback);
            char reason[128] = {};
            const int safe = cp0_desktop_exec_is_safe(exec->c_str(), reason, sizeof(reason));
            report(callback, safe ? 0 : -1, reason);
        } else if (command == "Shutdown") {
            if (!cp0_process_api_contract::has_exact_arguments(args, 1)) return invalid(callback);
            system_shutdown();
            report(callback, 0, "");
        } else if (command == "Reboot") {
            if (!cp0_process_api_contract::has_exact_arguments(args, 1)) return invalid(callback);
            system_reboot();
            report(callback, 0, "");
        } else if (command == "DelayMs") {
            int delay_ms = 0;
            const auto *text = cp0_process_api_contract::argument_at(args, 1);
            if (!cp0_process_api_contract::has_exact_arguments(args, 2) || !text ||
                !cp0_process_api_contract::parse_delay_ms(*text, delay_ms))
                return invalid(callback);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            report(callback, 0, "");
        } else {
            report(callback, -1, "unknown process api command");
        }
        } catch (...) {
            report(callback, -1, "process api failure");
        }
    }

    static int api_simple(const arg_t &args, std::string *data = nullptr)
    {
        int result = -1;
        cp0_signal_process_api(args, [&](int code, std::string output) {
            result = code;
            if (data) *data = std::move(output);
        });
        return result;
    }

private:
    static void report(const callback_t &callback, int code, const std::string &data)
    {
        cp0_process_api_contract::invoke_callback_safely(callback, code, data);
    }

    static void invalid(const callback_t &callback)
    {
        report(callback, -1, "invalid process api arguments");
    }

    static void system_shutdown()
    {
        std::printf("[SDL] shutdown requested (simulated no-op)\n");
    }

    static void system_reboot()
    {
        std::printf("[SDL] reboot requested (simulated no-op)\n");
    }
};

extern "C" void init_process(void)
{
    cp0_process_api_contract::invoke_c_api_void([] {
        static auto process = std::make_shared<ProcessSystem>();
        static cp0::SignalRegistration<decltype(cp0_signal_process_api)> registration;
        const auto active_process = process;
        registration.replace(cp0_signal_process_api,
            [active_process](std::list<std::string> args,
                      std::function<void(int, std::string)> callback) {
                active_process->api_call(std::move(args), std::move(callback));
            });
        });
}

extern "C" int cp0_process_exec_blocking(const char *exec_path, int keep_root)
{
    return cp0_process_api_contract::invoke_c_api([&] {
        return ProcessSystem::api_simple(
            {"ExecBlocking", exec_path ? exec_path : "", std::to_string(keep_root)});
    });
}

extern "C" cp0_pid_t cp0_process_spawn(const char *exec_path, int keep_root)
{
    const int result = cp0_process_api_contract::invoke_c_api([&] {
        std::string data;
        const int code = ProcessSystem::api_simple(
            {"Spawn", exec_path ? exec_path : "", std::to_string(keep_root)}, &data);
        int pid = 0;
        return code == 0 && cp0_process_api_contract::parse_spawn_response(data, pid) ? pid : -1;
    });
    return result < 0 ? static_cast<cp0_pid_t>(-1) : static_cast<cp0_pid_t>(result);
}

extern "C" void cp0_process_stop(cp0_pid_t pid)
{
    cp0_process_api_contract::invoke_c_api_void(
        [&] { ProcessSystem::api_simple({"Stop", std::to_string(pid)}); });
}

extern "C" int cp0_process_check_lock(const char *lock_path, int *holder_pid)
{
    if (holder_pid) *holder_pid = 0;
    return cp0_process_api_contract::invoke_c_api([&] {
        std::string data;
        const int result = ProcessSystem::api_simple(
            {"CheckLock", lock_path ? lock_path : ""}, &data);
        int parsed_pid = 0;
        if (holder_pid) {
            if (!cp0_process_api_contract::parse_lock_holder_response(data, parsed_pid)) return -1;
            *holder_pid = parsed_pid;
        }
        return result;
    });
}

extern "C" void cp0_process_kill(int pid, int grace_ms)
{
    cp0_process_api_contract::invoke_c_api_void([&] {
        ProcessSystem::api_simple({"Kill", std::to_string(pid), std::to_string(grace_ms)});
    });
}

extern "C" int cp0_process_run_argv(const char *const *argv, int background)
{
    return cp0_process_api_contract::invoke_c_api([&] {
        std::list<std::string> args = {"RunArgv", std::to_string(background)};
        if (argv) for (int index = 0; argv[index]; ++index) args.push_back(argv[index]);
        return ProcessSystem::api_simple(args);
    });
}

extern "C" int cp0_process_capture_argv(const char *const *argv, char *out, int out_size)
{
    if (out && out_size > 0) out[0] = '\0';
    return cp0_process_api_contract::invoke_c_api([&] {
        std::list<std::string> args = {"CaptureArgv"};
        if (argv) for (int index = 0; argv[index]; ++index) args.push_back(argv[index]);
        std::string data;
        const int result = ProcessSystem::api_simple(args, &data);
        if (out && out_size > 0) cp0_copy_string(out, out_size, data);
        return result;
    });
}

extern "C" int cp0_process_run_sudo(const char *password, const char *const *argv)
{
    return cp0_process_api_contract::invoke_c_api([&] {
        return ProcessSystem::api_simple(
            cp0_process_api_contract::make_run_sudo_request(password, argv));
    });
}

extern "C" int cp0_file_read_first_line(const char *path, char *out, int out_size)
{
    if (out && out_size > 0) out[0] = '\0';
    if (!path || !out || out_size <= 0) return -1;

    return cp0_process_api_contract::invoke_c_api([&] {
        std::ifstream file(path);
        if (!file.is_open()) return -1;
        std::string line;
        if (!std::getline(file, line)) return -1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        cp0_copy_string(out, out_size, line);
        return 0;
    });
}

extern "C" int cp0_desktop_exec_is_safe(const char *exec, char *reason, int reason_size)
{
    return cp0_process_api_contract::invoke_c_api([&] {
        const cp0_desktop_exec_policy::Result result = cp0_desktop_exec_policy::evaluate(exec);
        if (!result.allowed) cp0_copy_string(reason, reason_size, result.reason);
        return result.allowed ? 1 : 0;
    });
}

extern "C" void cp0_system_shutdown(void)
{
    cp0_process_api_contract::invoke_c_api_void(
        [] { ProcessSystem::api_simple({"Shutdown"}); });
}

extern "C" void cp0_system_reboot(void)
{
    cp0_process_api_contract::invoke_c_api_void(
        [] { ProcessSystem::api_simple({"Reboot"}); });
}

extern "C" int hal_process_exec_blocking(const char *exec_path, int keep_root)
{
    return cp0_process_exec_blocking(exec_path, keep_root);
}

extern "C" hal_pid_t hal_process_spawn(const char *exec_path, int keep_root)
{
    return cp0_process_spawn(exec_path, keep_root);
}

extern "C" void hal_process_stop(hal_pid_t pid)
{
    cp0_process_stop(pid);
}

extern "C" int hal_process_check_lock(const char *lock_path, int *holder_pid)
{
    return cp0_process_check_lock(lock_path, holder_pid);
}

extern "C" void hal_process_kill(int pid, int grace_ms)
{
    cp0_process_kill(pid, grace_ms);
}

extern "C" void hal_system_shutdown(void)
{
    cp0_system_shutdown();
}

extern "C" void hal_system_reboot(void)
{
    cp0_system_reboot();
}
