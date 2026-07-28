/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_lvgl_app_runner.hpp"
#include "sample_log.h"
#include "ui/ui.h"
#include "ui/ui_screensaver.h"

#include <string>
#include <utility>

#if CONFIG_BACKWARD_CPP_ENABLED
#define BACKWARD_HAS_DW 1
#include "backward.hpp"
#include "backward.h"
#endif

int main(void)
{
    Cp0LvglRunOptions options;
    options.after_resource_init = []() {
        cp0_signal_settings_api({"GpioSet", "BACKLIGHT", "0"}, [](int code, std::string data) {
            if (code == 0)
                SLOGI("[BOOT] set m5ioe1 line 9 low");
            else
                SLOGE("[BOOT] failed to set m5ioe1 line 9 low: %s", data.c_str());
        });
    };
    options.setup = []() {
        SLOGI("[BOOT] cp0_lvgl initialized");
        launcher_ui::init();
        ui_screensaver_init();
        return true;
    };
    options.teardown = []() { launcher_ui::deinit(); };
    return cp0_lvgl_run(std::move(options));
}
