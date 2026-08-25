#include "settings_t12b_adapter.hpp"

#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <list>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

namespace settings_t12b {
namespace {

bool operation_succeeded(int code, const std::string &data)
{
    return code == 0 && (data.empty() || data == "ok");
}

void mark_operation_started(void *data)
{
    if (!data) return;
    auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
    if (std::get<1>(*result))
        std::get<1>(*result)->store(true, std::memory_order_release);
}

void invoke_process(std::list<std::string> arguments,
                    std::function<void(int, std::string)> callback)
{
    try {
        cp0_signal_process_api(std::move(arguments), callback);
    } catch (...) {
        if (callback) callback(-1, "process api exception");
    }
}

void invoke_filesystem(std::list<std::string> arguments,
                       std::function<void(int, std::string)> callback)
{
    try {
        cp0_signal_filesystem_api(std::move(arguments), callback);
    } catch (...) {
        if (callback) callback(-1, "filesystem api exception");
    }
}

void invoke_settings(std::list<std::string> arguments,
                     std::function<void(int, std::string)> callback)
{
    try {
        cp0_signal_settings_api(std::move(arguments), callback);
    } catch (...) {
        if (callback) callback(-1, "settings api exception");
    }
}

} // namespace

struct BootActionController::State
{
    mutable std::mutex mutex;
    bool accepting = true;
    bool pending = false;
    uint64_t generation = 0;
    std::size_t operation_index = 0;
    boot_actions::Action action = boot_actions::Action::Reboot;
    Completion completion;
    SettingsAsync::Dispatch dispatch;
};

namespace {

using BootState = BootActionController::State;

bool boot_state_current(const std::shared_ptr<BootState> &state, uint64_t generation)
{
    if (!state) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->accepting && state->pending && state->generation == generation;
}

void finish_boot(const std::shared_ptr<BootState> &state,
                 uint64_t generation,
                 boot_actions::Operation operation,
                 bool succeeded,
                 int code,
                 std::string data)
{
    if (!state) return;

    BootResult result;
    BootActionController::Completion completion;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting || !state->pending || state->generation != generation)
            return;
        result.action = state->action;
        result.operation = operation;
        result.succeeded = succeeded;
        result.code = code;
        result.data = std::move(data);
        state->pending = false;
        completion = state->completion;
    }

    if (!completion) return;
    state->dispatch.enqueue([state, completion = std::move(completion), result = std::move(result)]() mutable {
        if (completion) completion(result);
    });
}

void run_boot_operation(const std::shared_ptr<BootState> &state, uint64_t generation);

void handle_boot_operation_result(const std::shared_ptr<BootState> &state,
                                  uint64_t generation,
                                  boot_actions::Operation operation,
                                  int code,
                                  std::string data)
{
    if (!state) return;
    const bool succeeded = operation_succeeded(code, data);
    bool continue_plan = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting || !state->pending || state->generation != generation)
            return;
        continue_plan = settings_t12b::boot_actions::may_continue(operation, succeeded);
        if (continue_plan) ++state->operation_index;
    }

    if (continue_plan) {
        run_boot_operation(state, generation);
        return;
    }
    finish_boot(state, generation, operation, succeeded, code, std::move(data));
}

void execute_filesystem_operation(const std::shared_ptr<BootState> &state,
                                  uint64_t generation,
                                  boot_actions::Operation operation,
                                  std::string path)
{
    const char *command = operation == boot_actions::Operation::RemoveLauncherSettings
                              ? "Remove"
                              : "Touch";
    invoke_filesystem(
        {command, std::move(path)},
        [state, generation, operation](int code, std::string data) mutable {
            handle_boot_operation_result(state, generation, operation, code, std::move(data));
        });
}

void resolve_filesystem_path(const std::shared_ptr<BootState> &state,
                             uint64_t generation,
                             boot_actions::Operation operation)
{
    const char *alias = settings_t12b::boot_actions::filesystem_alias(operation);
    if (!alias) {
        handle_boot_operation_result(state, generation, operation, -1, "missing filesystem alias");
        return;
    }

    invoke_filesystem(
        {"Path", alias},
        [state, generation, operation](int code, std::string path) mutable {
            if (code != 0 || path.empty()) {
                handle_boot_operation_result(
                    state, generation, operation, code == 0 ? -1 : code, std::move(path));
                return;
            }
            execute_filesystem_operation(state, generation, operation, std::move(path));
        });
}

void run_boot_operation(const std::shared_ptr<BootState> &state, uint64_t generation)
{
    if (!boot_state_current(state, generation)) return;

    boot_actions::Operation operation;
    bool has_operation = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto plan = settings_t12b::boot_actions::operation_plan(state->action);
        if (state->operation_index < plan.size()) {
            operation = plan[state->operation_index];
            has_operation = true;
        }
    }

    if (!has_operation) {
        finish_boot(state,
                    generation,
                    boot_actions::Operation::Reboot,
                    false,
                    -1,
                    "empty boot action plan");
        return;
    }

    const char *command = settings_t12b::boot_actions::process_command(operation);
    if (command) {
        invoke_process(
            {command},
            [state, generation, operation](int code, std::string data) mutable {
                handle_boot_operation_result(state, generation, operation, code, std::move(data));
            });
        return;
    }
    resolve_filesystem_path(state, generation, operation);
}

} // namespace

BootActionController::BootActionController(Completion completion)
    : state_(std::make_shared<State>())
{
    state_->completion = std::move(completion);
}

BootActionController::~BootActionController()
{
    auto state = std::move(state_);
    if (!state) return;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->accepting = false;
        state->pending = false;
        ++state->generation;
    }
    state->dispatch.cancel();
}

bool BootActionController::start(boot_actions::Action action)
{
    auto state = state_;
    if (!state) return false;

    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting || state->pending) return false;
        state->action = action;
        state->operation_index = 0;
        state->pending = true;
        generation = ++state->generation;
        if (generation == 0) generation = ++state->generation;
    }
    state->dispatch.advance_generation();
    run_boot_operation(state, generation);
    return true;
}

std::size_t BootActionController::poll()
{
    return state_ ? state_->dispatch.drain() : 0;
}

void BootActionController::cancel() noexcept
{
    auto state = state_;
    if (!state) return;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting) return;
        state->pending = false;
        ++state->generation;
    }
    state->dispatch.advance_generation();
}

bool BootActionController::pending() const noexcept
{
    auto state = state_;
    if (!state) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->accepting && state->pending;
}

BootActionBinding make_boot_action_binding(boot_actions::Action action,
                                           BootActionController::Completion completion)
{
    auto controller = std::make_shared<BootActionController>(std::move(completion));
    BootActionBinding binding;
    binding.controller = controller;
    binding.callback = [controller, action](int command, void *) {
        if (command == SettingApiActivate) controller->start(action);
    };
    return binding;
}

SettingApiCallBackFunc make_boot_confirmation_api(boot_actions::Action action,
                                                   bool confirmed,
                                                   BootActionController::Completion completion)
{
    if (!confirmed) return SettingApiCallBackFunc{[](int, void *) {}};
    return make_boot_action_binding(action, std::move(completion)).callback;
}

namespace {

struct ExtPortState
{
    mutable std::mutex mutex;
    bool value = false;
    bool initialized = false;
    bool pending = false;
    bool read_pending = false;
    uint64_t generation = 0;
};

void query_ext_port_async(extport::Port port,
                          std::function<void(bool, bool)> completion)
{
    const std::string name(extport::spec(port).api_name);
    invoke_settings(
        {"GpioGet", name},
        [completion = std::move(completion)](int code, std::string data) mutable {
            bool value = false;
            const bool succeeded = code == 0 && extport::parse_logical_value(data, value);
            if (completion) completion(succeeded, value);
        });
}

void send_ext_port_set(extport::Port port,
                       bool value,
                       std::function<void(int, std::string)> completion)
{
    const std::string name(extport::spec(port).api_name);
    invoke_settings({"GpioSet", name, value ? "1" : "0"}, std::move(completion));
}

void finish_ext_port_read(const std::shared_ptr<ExtPortState> &state,
                          uint64_t generation,
                          bool succeeded,
                          bool value)
{
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->generation != generation) return;
    state->read_pending = false;
    if (succeeded) {
        state->value = value;
        state->initialized = true;
    }
}

void start_ext_port_write(extport::Port port,
                          const std::shared_ptr<ExtPortState> &state,
                          uint64_t generation,
                          bool previous,
                          bool requested)
{
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->generation != generation || !state->pending) return;
        state->value = requested;
        state->initialized = true;
    }

    send_ext_port_set(
        port,
        requested,
        [state, port, generation, previous, requested](int code, std::string response) mutable {
            const bool succeeded = extport::gpio_set_succeeded(code, response);
            if (!succeeded) {
                send_ext_port_set(
                    port,
                    previous,
                    [state, port, generation, previous](int rollback_code,
                                                        std::string rollback_response) {
                        if (!extport::gpio_set_succeeded(rollback_code, rollback_response)) {
                            {
                                std::lock_guard<std::mutex> lock(state->mutex);
                                if (state->generation != generation || !state->pending) return;
                                state->read_pending = true;
                            }
                            query_ext_port_async(
                                port,
                                [state, generation, previous](bool read_succeeded, bool observed) {
                                    {
                                        std::lock_guard<std::mutex> lock(state->mutex);
                                        if (state->generation != generation || !state->pending) return;
                                        state->pending = false;
                                        state->read_pending = false;
                                        state->value = read_succeeded ? observed : previous;
                                        state->initialized = true;
                                    }
                                });
                            return;
                        }
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (state->generation != generation) return;
                        state->pending = false;
                        state->read_pending = false;
                        state->value = previous;
                        state->initialized = true;
                    });
                return;
            }

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->generation != generation || !state->pending) return;
                state->read_pending = true;
            }
            query_ext_port_async(
                port,
                [state, generation, requested](bool read_succeeded, bool observed) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->generation != generation || !state->pending) return;
                    state->pending = false;
                    state->read_pending = false;
                    state->value = read_succeeded ? observed : requested;
                    state->initialized = true;
                });
        });
}

} // namespace

SettingApiCallBackFunc make_ext_port_toggle_api(extport::Port port)
{
    auto state = std::make_shared<ExtPortState>();
    return [state, port](int command, void *data) {
        if (command == SettingApiReadFlag && data) {
            uint64_t generation = 0;
            bool should_query = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                should_query = !state->pending && !state->read_pending;
                generation = state->generation;
                if (should_query) state->read_pending = true;
            }
            if (should_query) {
                query_ext_port_async(
                    port,
                    [state, generation](bool succeeded, bool value) {
                        finish_ext_port_read(state, generation, succeeded, value);
                    });
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            *static_cast<bool *>(data) = state->value;
            return;
        }

        if (command == SettingApiReadFlagTimeStart && data) {
            mark_operation_started(data);
            auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
            uint64_t generation = 0;
            bool should_query = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                should_query = !state->pending && !state->read_pending;
                generation = state->generation;
                if (should_query) state->read_pending = true;
            }
            if (should_query) {
                query_ext_port_async(
                    port,
                    [state, generation](bool succeeded, bool value) {
                        finish_ext_port_read(state, generation, succeeded, value);
                    });
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            std::get<0>(*result) = state->value;
            return;
        }

        if (command != SettingApiActivate) return;

        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->pending || state->read_pending) return;
            state->pending = true;
            generation = ++state->generation;
            state->read_pending = true;
        }

        query_ext_port_async(
            port,
            [state, port, generation](bool succeeded, bool previous) {
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->generation != generation || !state->pending) return;
                    state->read_pending = false;
                    if (!succeeded) {
                        state->pending = false;
                        return;
                    }
                }
                start_ext_port_write(port, state, generation, previous, !previous);
            });
    };
}

std::vector<launcher::AppEntry> launcher_app_entries()
{
#ifdef LAUNCHER_BUILD
    std::size_t count = 0;
    const AppDescriptor *entries = launcher_app_registry_entries(&count);
    return launcher::configurable_entries(
        entries,
        count,
        [](const AppDescriptor &descriptor) {
            try {
                return launcher_app_registry_is_enabled(descriptor);
            } catch (...) {
                return descriptor.always_on;
            }
        });
#else
    return {};
#endif
}

bool populate_launcher_children(Tree &tree, const NodeIter &parent)
{
#ifdef LAUNCHER_BUILD
    tree.erase_children(parent);
    std::size_t count = 0;
    const AppDescriptor *entries = launcher_app_registry_entries(&count);
    if (count > 0 && !entries) return false;

    for (std::size_t index = 0; index < count; ++index) {
        const AppDescriptor &descriptor = entries[index];
        if (!descriptor.configurable || !descriptor.label || !descriptor.label[0] ||
            !descriptor.config_key || !descriptor.config_key[0])
            continue;
        tree.append_child(
            parent,
            SettingEntry{descriptor.label, make_launcher_toggle_api(descriptor), true});
    }
    return true;
#else
    (void)tree;
    (void)parent;
    return false;
#endif
}

#ifdef LAUNCHER_BUILD

namespace {

struct LauncherToggleState
{
    mutable std::mutex mutex;
    bool value = false;
    bool initialized = false;
    bool pending = false;
};

} // namespace

SettingApiCallBackFunc make_launcher_toggle_api(const AppDescriptor &descriptor)
{
    const std::string config_key = descriptor.config_key ? descriptor.config_key : "";
    const bool configurable = descriptor.configurable;
    const bool always_on = descriptor.always_on;
    auto state = std::make_shared<LauncherToggleState>();
    return [state, config_key, configurable, always_on](int command, void *data) {
        const AppDescriptor current_descriptor{nullptr,
                                               nullptr,
                                               config_key.c_str(),
                                               configurable,
                                               always_on};
        if (command == SettingApiReadFlag && data) {
            bool registry_value = false;
            bool registry_read = false;
            try {
                registry_value = launcher_app_registry_is_enabled(current_descriptor);
                registry_read = true;
            } catch (...) {
            }

            bool value = registry_value;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->pending) {
                    value = state->value;
                } else if (registry_read) {
                    state->value = registry_value;
                } else {
                    value = state->value;
                }
                state->initialized = true;
            }
            *static_cast<bool *>(data) = value;
            return;
        }

        if (command == SettingApiReadFlagTimeStart && data) {
            mark_operation_started(data);
            auto *result = static_cast<SettingApiReadFlagTimeStartData *>(data);
            bool registry_value = false;
            bool registry_read = false;
            try {
                registry_value = launcher_app_registry_is_enabled(current_descriptor);
                registry_read = true;
            } catch (...) {
            }

            bool value = registry_value;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->pending) {
                    value = state->value;
                } else if (registry_read) {
                    state->value = registry_value;
                } else {
                    value = state->value;
                }
                state->initialized = true;
            }
            std::get<0>(*result) = value;
            return;
        }

        if (command != SettingApiActivate) return;

        bool previous = false;
        bool registry_read = false;
        try {
            previous = launcher_app_registry_is_enabled(current_descriptor);
            registry_read = true;
        } catch (...) {
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->pending) return;
            if (!registry_read) previous = state->initialized ? state->value : false;
            state->pending = true;
            state->value = !previous;
            state->initialized = true;
        }

        bool succeeded = false;
        try {
            succeeded = launcher_app_registry_set_enabled(current_descriptor, !previous);
        } catch (...) {
            succeeded = false;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->pending = false;
            state->value = launcher::state_after_write(previous, !previous, succeeded);
        }
        if (launcher::should_notify_registry(succeeded))
            launcher_app_registry_notify_changed();
    };
}

struct LauncherRegistryRefreshSubscription::State
{
    mutable std::mutex mutex;
    bool accepting = true;
    bool attached = false;
    std::function<void()> refresh_callback;
    SettingsAsync::Dispatch dispatch;
    std::weak_ptr<State> self;
};

void LauncherRegistryRefreshSubscription::registry_changed(void *user_data)
{
    auto *raw_state = static_cast<State *>(user_data);
    if (!raw_state) return;

    std::shared_ptr<State> state;
    {
        std::lock_guard<std::mutex> lock(raw_state->mutex);
        if (!raw_state->accepting) return;
        state = raw_state->self.lock();
    }
    if (!state) return;

    state->dispatch.enqueue([state] {
        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->accepting) return;
            callback = state->refresh_callback;
        }
        if (callback) callback();
    });
}

LauncherRegistryRefreshSubscription::LauncherRegistryRefreshSubscription(
    std::function<void()> refresh_callback)
    : state_(std::make_shared<State>())
{
    state_->refresh_callback = std::move(refresh_callback);
    state_->self = state_;
}

LauncherRegistryRefreshSubscription::~LauncherRegistryRefreshSubscription()
{
    detach();
}

bool LauncherRegistryRefreshSubscription::attach()
{
    auto state = state_;
    if (!state) return false;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting || state->attached || !state->refresh_callback) return false;
    }

    launcher_app_registry_set_changed_callback(&registry_changed, state.get());
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting) {
            launcher_app_registry_clear_changed_callback(&registry_changed, state.get());
            return false;
        }
        state->attached = true;
    }
    return true;
}

std::size_t LauncherRegistryRefreshSubscription::poll()
{
    return state_ ? state_->dispatch.drain() : 0;
}

void LauncherRegistryRefreshSubscription::detach() noexcept
{
    auto state = std::move(state_);
    if (!state) return;

    launcher_app_registry_clear_changed_callback(&registry_changed, state.get());
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->accepting = false;
        state->attached = false;
        state->refresh_callback = {};
    }
    state->dispatch.cancel();
}

bool LauncherRegistryRefreshSubscription::attached() const noexcept
{
    auto state = state_;
    if (!state) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->attached && state->accepting;
}

#endif

void append_boot_action_child(Tree &tree,
                              const NodeIter &parent,
                              boot_actions::Action action,
                              const SettingPageFactory &confirmation_factory,
                              BootActionController::Completion completion)
{
    if (!confirmation_factory) return;

    const auto &view = boot_actions::presentation(action);
    const NodeIter action_node =
        tree.append_child(parent, SettingEntry{std::string(view.label), confirmation_factory});
    tree.append_child(
        action_node,
        SettingEntry{"Yes", make_boot_confirmation_api(action, true, std::move(completion)), false});
    tree.append_child(
        action_node,
        SettingEntry{"No", make_boot_confirmation_api(action, false), false});
}

void append_boot_children(Tree &tree,
                          const NodeIter &parent,
                          const SettingPageFactory &confirmation_factory)
{
    append_boot_action_child(
        tree, parent, boot_actions::Action::Reboot, confirmation_factory);
    append_boot_action_child(
        tree, parent, boot_actions::Action::Shutdown, confirmation_factory);
}

void append_ext_port_children(Tree &tree, const NodeIter &parent)
{
    for (const extport::Port port : {extport::Port::Grove5V, extport::Port::Ext5V}) {
        const auto &port_spec = extport::spec(port);
        tree.append_child(
            parent,
            SettingEntry{std::string(port_spec.label), make_ext_port_toggle_api(port), true});
    }
}

} // namespace settings_t12b
