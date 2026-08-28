/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl/lvgl.h"
#include "cp0_lvgl_app.h"
#include "app_directory_watcher.h"
#include "ui_loading.h"
#include "esc_ui_watchdog.h"
#include "model/launcher_navigation_model.hpp"

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

class Launch;
class UILaunchPage;

template <class PageT>
struct page_t
{
    using type = PageT;
};

template <class PageT>
inline constexpr page_t<PageT> page_v{};

struct app
{
    std::string Name;
    std::string Icon;
    std::string Exec;
    std::function<void(Launch *)> launch;

    app(std::string name, std::string icon, std::string exec, bool terminal);
    app(std::string name, std::string icon, std::string exec, bool terminal, bool sysplause);
    app(std::string name, std::string icon, std::string exec, bool terminal, bool sysplause, bool run_as_root);

    template <class PageT>
    app(std::string name, std::string icon, page_t<PageT> tag);
};

class Launch
{
public:
    Launch();
    ~Launch();

    void bind_ui();
    void set_launch_page(std::shared_ptr<UILaunchPage> launch_page);
    void select_next_app();
    void select_previous_app();
    const app *carousel_slot_app(size_t slot) const;
    void launch_app();

private:
    friend struct app;

    void go_back_home();
    bool begin_page_launch();
    void launch_Exec_in_terminal(const std::string &exec, bool sysplause = true);
    void launch_Exec(const std::string &exec, bool keep_root = false);
    void applications_load();
    void refresh_home_carousel();
    void reload_home_icons();
    void applications_reload();
    void rebuild_builtin_apps();
    int normalized_app_index(int index) const;
    const app *app_at_index(int index) const;

    static void lv_go_back_home(void *arg) noexcept;
    static void esc_force_home_cb(void *user_data) noexcept;
    static void app_registry_changed_cb(void *user_data) noexcept;

    std::weak_ptr<UILaunchPage> launch_page_;
    int current_app = 2;
    AppDirectoryWatcher app_directory_watcher_;
    EscUiWatchdog esc_ui_watchdog_;
    LauncherPageLifecycleModel page_lifecycle_;
    int fixed_count = 0;
    bool bound_ = false;
    std::list<app> app_list;
    std::shared_ptr<void> app_Page;
};

template <class PageT>
app::app(std::string name, std::string icon, page_t<PageT>)
    : Name(std::move(name)), Icon(std::move(icon))
{
    launch = [](Launch *owner) {
        if (!owner->begin_page_launch()) return;
        ui_loading::show("Loading...");
        lv_refr_now(nullptr);
        auto page = std::make_shared<PageT>();
        owner->app_Page = page;
        page->navigate_home = std::bind(&Launch::go_back_home, owner);
        ui_loading::hide();
        cp0_lvgl_start_app_page(*page);
    };
}
