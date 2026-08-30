#pragma once

#include "settings_system_model.hpp"
#include "settings_fonts.hpp"
#include "settings_tree_types.hpp"

#include <functional>
#include <memory>

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

enum class SettingsSystemPageKind
{
    Auto,
    Network,
    Ethernet,
    Account,
    Password,
    OS,
    Version,
    Build,
};

class LvSettingSystemInfoPage3 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        ScreenW       = 320,
        ScreenH       = 150,
        TextX         = 8,
        TextW         = 304,
        TitleY        = 3,
        LineOneY      = 31,
        LineTwoY      = 53,
        LineThreeY    = 75,
        StatusY       = 99,
        HintY         = 133,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingSystemInfoPage3() = default;
    LvSettingSystemInfoPage3(lv_obj_t *parent,
                             const NodeIter &page_node,
                             std::function<void()> back_callback,
                             SettingsSystemPageKind kind = SettingsSystemPageKind::Auto);
    ~LvSettingSystemInfoPage3() override;

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;
    void create_ui(lv_obj_t *parent) override;

private:
    struct State;

public:
    std::unique_ptr<State> state_;
};

class LvSettingUpdatePage3 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        ScreenW    = 320,
        ScreenH    = 150,
        TextX      = 8,
        TextW      = 304,
        TitleY     = 2,
        DeviceY    = 22,
        LvglY      = 39,
        VersionY   = 56,
        BuildY     = 73,
        CommitY    = 90,
        StatusY    = 108,
        HintY      = 133,
        DialogW    = 296,
        DialogH    = 132,
        DialogRadius = 4,
        DialogBorderWidth = 1,
        ProgressBarH = 10,
        ContentPadRow = 8,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingUpdatePage3() = default;
    LvSettingUpdatePage3(lv_obj_t *parent,
                         const NodeIter &page_node,
                         std::function<void()> back_callback,
                         settings_system::UpdateAction action = settings_system::UpdateAction::UpdateLauncher);
    ~LvSettingUpdatePage3() override;

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;
    void create_ui(lv_obj_t *parent) override;

private:
    struct State;

public:
    std::unique_ptr<State> state_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_system_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback);

std::unique_ptr<DComponens::LvglComponensBase> settings_ethernet_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback);

std::unique_ptr<DComponens::LvglComponensBase> settings_account_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback);

std::unique_ptr<DComponens::LvglComponensBase> settings_update_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback);

std::unique_ptr<DComponens::LvglComponensBase> settings_system_info_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback);

std::unique_ptr<DComponens::LvglComponensBase> settings_ethernet_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback);

std::unique_ptr<DComponens::LvglComponensBase> settings_account_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback);

std::unique_ptr<DComponens::LvglComponensBase> settings_update_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback);
