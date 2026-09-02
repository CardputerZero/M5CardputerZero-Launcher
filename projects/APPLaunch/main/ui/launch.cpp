/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launch.h"

#include "app_registry.h"
#include "builtin_app_registry.hpp"
#include "desktop_app_loader.hpp"
#include "esc_hold_hint_controller.h"
#include "ui.h"
#include "generated/page_app.h"
#include "ui_launch_page.h"
#include "ui_screensaver.h"
#include "ui_loading.h"
#include "cp0_lvgl_app.h"
#include "sample_log.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace {
constexpr size_t kHomeCarouselSlotCount = 5;
constexpr int kHomeCarouselCenterSlot = 2;
}

// ============================================================
// Launch
// ============================================================
void Launch::bind_ui()
{
    if (bound_) {
        refresh_home_carousel();
        return;
    }
    bound_ = true;

    launcher_app_registry_set_changed_callback(app_registry_changed_cb, this);
    rebuild_builtin_apps();
    applications_load();
    reload_home_icons();

    app_directory_watcher_.start([this] { applications_reload(); });
    esc_ui_watchdog_.start();
    esc_hold_hint_controller().set_force_home_callback(esc_force_home_cb, this);
}

void Launch::launch_app()
{
    const app *selected = app_at_index(current_app);
    if (selected) selected->launch(this);
}

void Launch::lv_go_back_home(void *arg) noexcept
{
    auto *self = static_cast<Launch *>(arg);
    if (!self || !self->page_lifecycle_.complete_home()) return;
    try {
        esc_hold_hint_controller().set_return_home_enabled(false);
        SLOGI("[HOME] lv_go_back_home executing (page=%p)", self->app_Page.get());
        lv_timer_enable(true);
        if (auto page = self->launch_page_.lock()) page->show_home_screen();
        lv_refr_now(nullptr);
        self->app_Page.reset();
        self->esc_ui_watchdog_.disarm();
        SLOGI("[HOME] lv_go_back_home done, on launcher home");
    } catch (...) {
        self->app_Page.reset();
        self->esc_ui_watchdog_.disarm();
    }
}

void Launch::go_back_home()
{
    if (!page_lifecycle_.request_home()) return;
    esc_hold_hint_controller().set_return_home_enabled(false);
    SLOGI("[HOME] go_back_home() requested, scheduling async call (page=%p)", app_Page.get());
    if (lv_async_call(lv_go_back_home, this) != LV_RESULT_OK) {
        page_lifecycle_.cancel_home_request();
        esc_hold_hint_controller().set_return_home_enabled(true);
    }
}

bool Launch::begin_page_launch()
{
    if (!page_lifecycle_.begin_app()) return false;
    esc_hold_hint_controller().set_return_home_enabled(true);
    esc_ui_watchdog_.arm();
    return true;
}

void Launch::launch_Exec_in_terminal(const std::string &exec, bool sysplause)
{
    if (!begin_page_launch()) return;
    SLOGI("Launching terminal app: %s", exec.c_str());
    ui_loading::show("Loading...");
    lv_refr_now(nullptr);
    auto p = std::make_shared<UISTPage>();
    app_Page = p;
    p->navigate_home = std::bind(&Launch::go_back_home, this);
    p->terminal_sysplause = sysplause;
    ui_loading::hide();
    cp0_lvgl_start_app_page(*p);
    p->exec(exec);
}

void Launch::launch_Exec(const std::string &exec, bool keep_root)
{
    SLOGI("Launching external app: %s (keep_root=%d)", exec.c_str(), keep_root);
    ui_loading::show("Loading...");
    lv_disp_t *disp = lv_disp_get_default();
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    ui_screensaver_set_foreground(0);
    LVGL_RUN_FLAGE = 0;
    if (indev) lv_indev_set_group(indev, nullptr);
    lv_refr_now(disp);
    // External apps own the framebuffer exclusively. Keep process supervision
    // active in the backend, but stop every Launcher timer/flush until the
    // child exits; drawing an LVGL toast here would overwrite the child UI.
    lv_timer_enable(false);
    int result = -1;
    try {
        cp0_signal_process_api(
            {"ExecBlocking", exec, keep_root ? "1" : "0"},
            [&](int code, std::string) { result = code; });
    } catch (...) {
        result = -1;
    }
    SLOGI("External app exited with code %d", result);

    lv_timer_enable(true);
    if (indev) lv_indev_set_group(indev, UILaunchPage::home_input_group());
    if (auto page = launch_page_.lock()) page->show_home_screen();
    ui_loading::hide();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    LVGL_RUN_FLAGE = 1;
    ui_screensaver_set_foreground(1);
}

void Launch::select_next_app()
{
    int next = normalized_app_index(current_app + 1);
    if (next >= 0) current_app = next;
}

void Launch::select_previous_app()
{
    int previous = normalized_app_index(current_app - 1);
    if (previous >= 0) current_app = previous;
}

std::size_t Launch::app_count() const
{
    return app_list.size();
}

std::size_t Launch::current_app_index() const
{
    const int normalized = normalized_app_index(current_app);
    return normalized < 0 ? 0 : static_cast<std::size_t>(normalized);
}

const app *Launch::carousel_slot_app(size_t slot) const
{
    if (slot >= kHomeCarouselSlotCount)
        return nullptr;
    return app_at_index(current_app + static_cast<int>(slot) - kHomeCarouselCenterSlot);
}

void Launch::applications_load()
{
    launcher_append_desktop_apps(app_list);
    launcher_sort_app_display_order(app_list);
}

void Launch::refresh_home_carousel()
{
    int normalized = normalized_app_index(current_app);
    if (normalized < 0) return;
    current_app = normalized;
    if (auto page = launch_page_.lock()) page->refresh_carousel();
}

void Launch::reload_home_icons()
{
    const int normalized = normalized_app_index(current_app);
    if (normalized >= 0) current_app = normalized;

    std::vector<std::string> icon_paths;
    icon_paths.reserve(app_list.size());
    for (const app &item : app_list)
        icon_paths.push_back(item.Icon);
    if (auto page = launch_page_.lock()) page->reload_home_icons(icon_paths);
}

void Launch::applications_reload()
{
    rebuild_builtin_apps();
    applications_load();
    reload_home_icons();
}

int Launch::normalized_app_index(int index) const
{
    int size = static_cast<int>(app_list.size());
    if (size == 0)
        return -1;

    index %= size;
    return index < 0 ? index + size : index;
}

const app *Launch::app_at_index(int index) const
{
    int normalized = normalized_app_index(index);
    if (normalized < 0)
        return nullptr;

    return &*std::next(app_list.begin(), normalized);
}

void Launch::esc_force_home_cb(void *user_data) noexcept
{
    auto *self = static_cast<Launch *>(user_data);
    if (!self || !self->app_Page || LVGL_RUN_FLAGE == 0) return;
    try {
        SLOGW("[HOME] ESC held for 3000 ms, forcing built-in page home");
        self->go_back_home();
    } catch (...) {
    }
}


Launch::~Launch()
{
    launcher_app_registry_clear_changed_callback(app_registry_changed_cb, this);
    esc_hold_hint_controller().set_force_home_callback(nullptr, nullptr);
    esc_hold_hint_controller().set_return_home_enabled(false);
    esc_ui_watchdog_.shutdown();
    app_directory_watcher_.stop();
    page_lifecycle_.stop();
    lv_async_call_cancel(lv_go_back_home, this);
}

Launch::Launch() = default;

void Launch::set_launch_page(std::shared_ptr<UILaunchPage> launch_page)
{
    launch_page_ = std::move(launch_page);
}

void Launch::rebuild_builtin_apps()
{
    app_list.clear();

    launcher_append_enabled_builtin_apps(app_list);

    fixed_count = app_list.size();
    current_app = normalized_app_index(current_app);
}

void Launch::app_registry_changed_cb(void *user_data) noexcept
{
    auto *self = static_cast<Launch *>(user_data);
    if (!self) return;
    try {
        self->applications_reload();
    } catch (...) {
    }
}
