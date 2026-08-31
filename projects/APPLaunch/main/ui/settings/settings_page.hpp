#pragma once

#include <functional>
#include <memory>

#include "settings_tree_types.hpp"
#include "ui_app_page.hpp"

class LvSettingRoller;

class UISettingTreePage : public AppPage {
public:
    UISettingTreePage();
    ~UISettingTreePage() override;

private:
    void create_page_detail();
    static void back_home(void *data);
    void LeaveNextPage();
    void AnimateNextIn(std::function<void()> animate_over_func);
    void AnimateNextOut(std::function<void()> animate_over_func);
    void LoadNextPage();

    Tree mode_tree_;
    std::unique_ptr<LvSettingRoller> roller_;
};
