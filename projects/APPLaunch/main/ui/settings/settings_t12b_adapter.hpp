#pragma once

#include "settings_t12b_boot_action_policy.hpp"
#include "settings_t12b_extport_model.hpp"
#include "settings_t12b_launcher_model.hpp"
#include "settings_tree_types.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef LAUNCHER_BUILD
#include "app_registry.h"
#endif

namespace settings_t12b {

struct BootResult
{
    boot_actions::Action action = boot_actions::Action::Reboot;
    boot_actions::Operation operation = boot_actions::Operation::Reboot;
    bool succeeded = false;
    int code = -1;
    std::string data;
};

class BootActionController
{
public:
    struct State;
    using Completion = std::function<void(const BootResult &)>;

    explicit BootActionController(Completion completion = {});
    ~BootActionController();

    BootActionController(const BootActionController &) = delete;
    BootActionController &operator=(const BootActionController &) = delete;

    bool start(boot_actions::Action action);
    std::size_t poll();
    void cancel() noexcept;
    bool pending() const noexcept;

private:
    std::shared_ptr<State> state_;
};

struct BootActionBinding
{
    SettingApiCallBackFunc callback;
    std::shared_ptr<BootActionController> controller;
};

BootActionBinding make_boot_action_binding(boot_actions::Action action,
                                           BootActionController::Completion completion = {});
SettingApiCallBackFunc make_boot_confirmation_api(boot_actions::Action action,
                                                   bool confirmed,
                                                   BootActionController::Completion completion = {});

SettingApiCallBackFunc make_ext_port_toggle_api(extport::Port port);

std::vector<launcher::AppEntry> launcher_app_entries();
bool populate_launcher_children(Tree &tree, const NodeIter &parent);

void append_boot_children(Tree &tree,
                          const NodeIter &parent,
                          const SettingPageFactory &confirmation_factory);
void append_boot_action_child(Tree &tree,
                              const NodeIter &parent,
                              boot_actions::Action action,
                              const SettingPageFactory &confirmation_factory,
                              BootActionController::Completion completion = {});
void append_ext_port_children(Tree &tree, const NodeIter &parent);

#ifdef LAUNCHER_BUILD
SettingApiCallBackFunc make_launcher_toggle_api(const AppDescriptor &descriptor);

class LauncherRegistryRefreshSubscription
{
public:
    explicit LauncherRegistryRefreshSubscription(std::function<void()> refresh_callback);
    ~LauncherRegistryRefreshSubscription();

    LauncherRegistryRefreshSubscription(const LauncherRegistryRefreshSubscription &) = delete;
    LauncherRegistryRefreshSubscription &operator=(const LauncherRegistryRefreshSubscription &) = delete;

    bool attach();
    std::size_t poll();
    void detach() noexcept;
    bool attached() const noexcept;

private:
    struct State;
    static void registry_changed(void *user_data);

    std::shared_ptr<State> state_;
};
#endif

} // namespace settings_t12b
