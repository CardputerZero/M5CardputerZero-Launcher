#include "application.h"
#include "cp0_lvgl_app_runner.hpp"

#include <utility>

int lvgl_main(void)
{
    Cp0LvglRunOptions options;
    options.after_lvgl_init = [] { launch_wizard_register_event(); };
    options.setup = [] { return launch_wizard_ui_setup(); };
    options.should_quit = [] { return launch_wizard_should_quit(); };
    options.teardown = [] { launch_wizard_ui_teardown(); };
    return cp0_lvgl_run(std::move(options));
}
