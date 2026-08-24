#include "cp0_lvgl_app_runner.hpp"
#include "setting_tree.hpp"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return cp0_lvgl_run_app<UISettingTreePage>();
}
