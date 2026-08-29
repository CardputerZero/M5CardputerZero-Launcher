#pragma once

#include "settings_system_model.hpp"
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
    struct State;

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

    std::unique_ptr<State> state_;
};

class LvSettingUpdatePage3 : public DComponens::LvglComponensBase {
public:
    struct State;

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
