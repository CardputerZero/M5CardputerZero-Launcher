/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl/lvgl.h"

class LauncherToast
{
public:
    void show(const char *text) noexcept;
    void show_persistent(const char *text) noexcept;
    void hide();
    void shutdown();

private:
    bool ensure_created() noexcept;
    static void hide_timer_cb(lv_timer_t *timer) noexcept;
    static void container_delete_cb(lv_event_t *event) noexcept;
    static void label_delete_cb(lv_event_t *event) noexcept;

    lv_obj_t *container_ = nullptr;
    lv_obj_t *label_ = nullptr;
    lv_timer_t *hide_timer_ = nullptr;
};

LauncherToast &launcher_toast();
