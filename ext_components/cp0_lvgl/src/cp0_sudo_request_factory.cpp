#include "cp0_sudo_request_factory.hpp"

#include <cerrno>

namespace cp0_sudo {

bool RequestFactory::valid_thread(cp0_sudo_callback_thread_t thread)
{
    return thread == CP0_SUDO_CALLBACK_LVGL || thread == CP0_SUDO_CALLBACK_WORKER;
}

std::shared_ptr<Request> RequestFactory::create(
    cp0_sudo_callback_thread_t callback_thread, cp0_sudo_output_cb_t output_cb,
    cp0_sudo_complete_cb_t complete_cb, void *user)
{
    auto request = std::make_shared<Request>();
    request->id = next_id_.fetch_add(1);
    request->callback_thread = callback_thread;
    request->output_cb = output_cb;
    request->complete_cb = complete_cb;
    request->user = user;
    request->auth_timeout_ms = kDefaultAuthTimeoutMs;
    request->exec_timeout_ms = kDefaultExecTimeoutMs;
    request->queue_timeout_ms = kDefaultQueueTimeoutMs;
    return request;
}

RequestBuildResult RequestFactory::create_argv(
    const char *const *argv, cp0_sudo_callback_thread_t callback_thread,
    cp0_sudo_output_cb_t output_cb, cp0_sudo_complete_cb_t complete_cb,
    void *user, int auth_timeout_ms, int exec_timeout_ms)
{
    if (!argv || !argv[0] || !argv[0][0] || !valid_thread(callback_thread))
        return {-EINVAL, {}};

    auto request = create(callback_thread, output_cb, complete_cb, user);
    if (auth_timeout_ms > 0) request->auth_timeout_ms = auth_timeout_ms;
    if (exec_timeout_ms > 0) request->exec_timeout_ms = exec_timeout_ms;
    for (size_t i = 0; argv[i]; ++i) request->argv.emplace_back(argv[i]);
    return {0, std::move(request)};
}

RequestBuildResult RequestFactory::create_shell(
    const char *command, cp0_sudo_callback_thread_t callback_thread,
    cp0_sudo_output_cb_t output_cb, cp0_sudo_complete_cb_t complete_cb,
    void *user)
{
    if (!command || !command[0] || !valid_thread(callback_thread))
        return {-EINVAL, {}};

    auto request = create(callback_thread, output_cb, complete_cb, user);
    request->argv.emplace_back(command);
    request->use_login_shell = true;
    return {0, std::move(request)};
}

} // namespace cp0_sudo
