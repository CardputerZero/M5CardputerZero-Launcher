#pragma once

#include <functional>
#include <memory>

#include "settings_menu_roller.hpp"
#include "settings_tree_types.hpp"
#include "ui_app_page.hpp"
#ifdef LAUNCHER_BUILD
#include "app_registry.h"
#endif

void adb_guide_api(int cmd, void *data);
void adb_toggle_api(int cmd, void *data);

class UISettingTreePage : public AppPage {
public:
    Tree mode_tree;
    std::unique_ptr<LvSettingRoller> roller1_ = nullptr;

    void create_page_detail();
    static void _back_home(void *data);
    void LeaveNextPage();
    void AnimateNextIn(std::function<void()> animate_over_func);
    void AnimateNextOut(std::function<void()> animate_over_func);
    void LoadNextPage();

    UISettingTreePage();
    ~UISettingTreePage() override;
};
