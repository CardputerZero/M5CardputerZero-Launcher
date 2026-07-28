#include "../src/cp0_sudo_request_factory.hpp"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <string>

using cp0_sudo::RequestFactory;

namespace {

void output_callback(const char *, size_t, void *) {}
void complete_callback(cp0_sudo_result_t, int, void *) {}

} // namespace

int main()
{
    RequestFactory factory(41);
    const char *argv[] = {"program", "", "argument", nullptr};
    int user = 7;
    auto built = factory.create_argv(argv, CP0_SUDO_CALLBACK_WORKER,
        output_callback, complete_callback, &user, 1200, 3400);
    assert(built.error == 0);
    assert(built.request);
    assert(built.request->id == 41);
    assert(built.request->argv.size() == 3);
    assert(built.request->argv[1].empty());
    assert(built.request->callback_thread == CP0_SUDO_CALLBACK_WORKER);
    assert(built.request->output_cb == output_callback);
    assert(built.request->complete_cb == complete_callback);
    assert(built.request->user == &user);
    assert(built.request->auth_timeout_ms == 1200);
    assert(built.request->exec_timeout_ms == 3400);
    assert(built.request->queue_timeout_ms == cp0_sudo::kDefaultQueueTimeoutMs);
    assert(!built.request->use_login_shell);

    auto shell = factory.create_shell("printf ok", CP0_SUDO_CALLBACK_LVGL,
        nullptr, nullptr, nullptr);
    assert(shell.error == 0);
    assert(shell.request->id == 42);
    assert(shell.request->argv == std::vector<std::string>{"printf ok"});
    assert(shell.request->use_login_shell);
    assert(shell.request->auth_timeout_ms == cp0_sudo::kDefaultAuthTimeoutMs);
    assert(shell.request->exec_timeout_ms == cp0_sudo::kDefaultExecTimeoutMs);
    assert(shell.request->queue_timeout_ms == cp0_sudo::kDefaultQueueTimeoutMs);

    auto defaults = factory.create_argv(argv, CP0_SUDO_CALLBACK_LVGL,
        nullptr, nullptr, nullptr, 0, 0);
    assert(defaults.request->auth_timeout_ms == cp0_sudo::kDefaultAuthTimeoutMs);
    assert(defaults.request->exec_timeout_ms == cp0_sudo::kDefaultExecTimeoutMs);

    const char *empty_argv[] = {"", nullptr};
    assert(factory.create_argv(nullptr, CP0_SUDO_CALLBACK_LVGL, nullptr,
        nullptr, nullptr, 0, 0).error == -EINVAL);
    assert(factory.create_argv(empty_argv, CP0_SUDO_CALLBACK_LVGL, nullptr,
        nullptr, nullptr, 0, 0).error == -EINVAL);
    assert(factory.create_argv(argv, static_cast<cp0_sudo_callback_thread_t>(99),
        nullptr, nullptr, nullptr, 0, 0).error == -EINVAL);
    assert(factory.create_shell(nullptr, CP0_SUDO_CALLBACK_LVGL, nullptr,
        nullptr, nullptr).error == -EINVAL);
    assert(factory.create_shell("", CP0_SUDO_CALLBACK_LVGL, nullptr,
        nullptr, nullptr).error == -EINVAL);
    assert(factory.create_shell("true", static_cast<cp0_sudo_callback_thread_t>(99),
        nullptr, nullptr, nullptr).error == -EINVAL);

    auto next = factory.create_shell("true", CP0_SUDO_CALLBACK_LVGL,
        nullptr, nullptr, nullptr);
    assert(next.request->id == 44);
}
