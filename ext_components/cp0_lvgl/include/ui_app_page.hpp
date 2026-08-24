/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl/lvgl.h"
#include "cp0_lvgl_app.h"
#include "cp0_lvgl_app_page_assets.h"
#include "hal_lvgl_bsp.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

class AppTopBarRegion;
class AppTopBarComponent;
namespace cp0::status { class Lifecycle; }

/**
 * @file ui_app_page.hpp
 * @brief Page, content-region, top-bar, and bottom-bar building blocks.
 *
 * Page inheritance guide:
 * - Inherit from AppPageRoot for the smallest possible blank LVGL screen.
 * - Inherit from AppPage for a normal application page with a content region
 *   and an enabled top bar.
 * - Inherit from AppPageWithBottomBarLayout when a page also needs the bottom
 *   bar region. This class is a layout mixin; add it to your own page class.
 *
 * AppPageRoot creates and owns the LVGL screen. The screen is available from
 * screen() after the base constructor has completed. A page is normally
 * shown with cp0_lvgl_start_app_page(page) or the owning helper
 * cp0_lvgl_start_app<PageT>().
 *
 * Minimal blank-page example:
 * @code
 * class BlankPage final : public AppPageRoot
 * {
 * public:
 *     BlankPage()
 *     {
 *         lv_obj_set_style_bg_color(screen(), lv_color_hex(0x101010), 0);
 *     }
 * };
 *
 * auto page = cp0_lvgl_start_app<BlankPage>();
 * @endcode
 *
 * Use AppPage instead when the page should participate in the standard
 * application layout:
 * @code
 * class StandardPage final : public AppPage
 * {
 * public:
 *     StandardPage()
 *     {
 *         set_page_title("Standard page");
 *         lv_obj_t *label = lv_label_create(ui_APP_Container);
 *         lv_label_set_text(label, "Hello");
 *         lv_obj_center(label);
 *     }
 * };
 * @endcode
 */

/**
 * @brief Owns the reusable application content container.
 *
 * This helper is normally used by AppContentRegion. Application pages should
 * usually inherit from AppPageRoot or AppPage instead of using this class
 * directly.
 */
class UIAppContainer
{
public:
    UIAppContainer() = default;
    explicit UIAppContainer(int height);
    ~UIAppContainer();

    void set_height(int height);
    lv_obj_t *create(lv_obj_t *parent);

    lv_obj_t *get() const { return container_; }

private:
    static void container_delete_cb(lv_event_t *event);
    int height_ = 150;
    lv_obj_t *container_ = nullptr;
};

/**
 * @brief Minimal page base that owns one LVGL screen and its input group.
 *
 * Derive directly from this class to create a blank or custom screen without
 * the standard content-area and top-bar layout. The constructor creates the
 * screen, and screen() returns the root LVGL object for custom UI creation.
 */
class AppPageRoot
{
public:
    AppPageRoot();
    virtual ~AppPageRoot();

    AppPageRoot(const AppPageRoot &) = delete;
    AppPageRoot &operator=(const AppPageRoot &) = delete;
    AppPageRoot(AppPageRoot &&) = delete;
    AppPageRoot &operator=(AppPageRoot &&) = delete;

    lv_obj_t *screen() const { return root_screen_; }
    lv_group_t *input_group() const { return input_group_; }

    void enable_top_bar();
    void disable_top_bar();
    bool top_bar_enabled() const;
    void set_page_title(const std::string &title);
    void update_time();
    void update_status();
    void update_battery(const cp0_battery_info_t &battery);
    void update_datetime_status() { update_time(); }
    void update_status_bar() { update_status(); }
    void update_battery_status(const cp0_battery_info_t &battery) { update_battery(battery); }
    AppTopBarComponent *add_top_bar_component(
        std::unique_ptr<AppTopBarComponent> component);
    bool remove_top_bar_component(const std::string &id);
    AppTopBarComponent *top_bar_component(const std::string &id) const;
    void clear_content();

    template <typename Component>
    lv_obj_t *add_bar(Component &&component)
    {
        return component.create(root_screen_);
    }

    std::string page_title_ = "APP";
    lv_group_t *input_group_ = nullptr;
    lv_obj_t *root_screen_ = nullptr;
    std::function<void()> navigate_home;
    bool has_bottom_bar_ = false;
    int top_bar_height_px_ = 20;

private:
    static void screen_delete_cb(lv_event_t *event);
    void create_base_ui();
    void create_input_group();
    void release_root_screen();

    std::unique_ptr<AppTopBarRegion> top_bar_region_;
};

/**
 * @brief Adds the standard application content container to AppPageRoot.
 *
 * The content container is available as ui_APP_Container. This class is used
 * as a layout building block and is normally reached through AppPage.
 */
class AppContentRegion : virtual public AppPageRoot
{
public:
    AppContentRegion();

    virtual ~AppContentRegion();

    void refresh();

    // Kept for source compatibility with existing applications.
    void refash() { refresh(); }

    lv_obj_t *ui_APP_Container = nullptr;

private:
    static void container_delete_cb(lv_event_t *event);
    UIAppContainer app_container_;
};

/**
 * @brief Adds the standard bottom-bar container to a page layout.
 *
 * The bottom-bar object is available as ui_BOTTOM_Container. Use this as a
 * virtual layout base when a custom page needs a bottom navigation region.
 */
class AppBottomBarRegion : virtual public AppPageRoot, virtual public AppContentRegion
{
public:
    AppBottomBarRegion();

    virtual ~AppBottomBarRegion();

    lv_obj_t *ui_BOTTOM_Container = nullptr;

private:
    static void container_delete_cb(lv_event_t *event);
};

/**
 * @brief Base class for a component mounted inside the application's top bar.
 *
 * Derived components implement on_create() to populate the object returned by
 * obj(). The top-bar manager owns the LVGL object and controls its visibility.
 */
class AppTopBarComponent
{
public:
    // Components live inside a 20 px bar with 2 px vertical clearance.
    static constexpr int kMaximumHeight = 16;
    // Used when a component does not provide a useful width recommendation.
    static constexpr int kDefaultRecommendedWidth = 20;

    explicit AppTopBarComponent(std::string id,
                                int recommended_width = kDefaultRecommendedWidth,
                                int height = kMaximumHeight);
    virtual ~AppTopBarComponent();

    AppTopBarComponent(const AppTopBarComponent &) = delete;
    AppTopBarComponent &operator=(const AppTopBarComponent &) = delete;
    AppTopBarComponent(AppTopBarComponent &&) = delete;
    AppTopBarComponent &operator=(AppTopBarComponent &&) = delete;

    const std::string &id() const { return id_; }
    int height() const { return height_; }
    int recommended_width() const { return recommended_width_; }
    lv_obj_t *obj() const { return obj_; }

    void set_visible(bool visible);
    bool visible() const;

    virtual void activate() {}
    virtual void deactivate() {}
    virtual void refresh() {}

protected:
    // The base class creates and owns obj. Derived components add their LVGL
    // children here and can use obj() later for state updates.
    virtual void on_create(lv_obj_t *obj) = 0;
    virtual void on_object_deleted() {}
    virtual bool resources_ready() const { return true; }
    void set_width(int width);
    void unmount();

private:
    friend class UIAppTopBar;

    static void object_delete_cb(lv_event_t *event);
    lv_obj_t *mount(lv_obj_t *parent);
    void detach_from_deleted_parent();

    std::string id_;
    int recommended_width_;
    int height_;
    lv_obj_t *obj_ = nullptr;
};

/** @brief Built-in top-bar component that displays the current page title. */
class AppTopBarTitleComponent final : public AppTopBarComponent
{
public:
    AppTopBarTitleComponent();
    ~AppTopBarTitleComponent() override;
    void set_title(const std::string &title);

protected:
    void on_create(lv_obj_t *obj) override;
    void on_object_deleted() override;
    bool resources_ready() const override;

private:
    static void child_delete_cb(lv_event_t *event);
    std::string title_ = "APP";
    lv_obj_t *label_ = nullptr;
};

/** @brief Built-in top-bar component that displays the current time. */
class AppTopBarTimeComponent final : public AppTopBarComponent
{
public:
    AppTopBarTimeComponent();
    ~AppTopBarTimeComponent() override;
    void refresh() override;

protected:
    void on_create(lv_obj_t *obj) override;
    void on_object_deleted() override;
    bool resources_ready() const override;

private:
    static void child_delete_cb(lv_event_t *event);
    lv_obj_t *label_ = nullptr;
};

/** @brief Built-in top-bar component that displays battery state. */
class AppTopBarBatteryComponent final : public AppTopBarComponent
{
public:
    AppTopBarBatteryComponent();
    ~AppTopBarBatteryComponent() override;
    void update(const cp0_battery_info_t &battery);
    void deactivate() override;

protected:
    void on_create(lv_obj_t *obj) override;
    void on_object_deleted() override;
    bool resources_ready() const override;

private:
    static void child_delete_cb(lv_event_t *event);
    static void blink_cb(void *object, int32_t opacity);
    void set_charging(bool charging);
    void set_low_battery_blink(bool enabled);

    bool charging_ = false;
    bool low_battery_blink_ = false;
    lv_obj_t *battery_bar_ = nullptr;
    lv_obj_t *charge_wave_ = nullptr;
    lv_obj_t *label_ = nullptr;
};

/** @brief Built-in top-bar component that displays network state. */
class AppTopBarNetworkComponent final : public AppTopBarComponent
{
public:
    AppTopBarNetworkComponent();
    ~AppTopBarNetworkComponent() override;
    void refresh() override;

protected:
    void on_create(lv_obj_t *obj) override;
    void on_object_deleted() override;
    bool resources_ready() const override;

private:
    static void child_delete_cb(lv_event_t *event);
    lv_obj_t *ethernet_image_ = nullptr;
    lv_obj_t *wifi_panel_ = nullptr;
    lv_obj_t *wifi_bars_[4] = {};
};

/**
 * @brief Owns and manages the top-bar container and its components.
 *
 * Pages normally access this functionality through AppPageRoot rather than
 * constructing UIAppTopBar directly.
 */
class UIAppTopBar
{
public:
    UIAppTopBar();
    ~UIAppTopBar();
    void set_height(int height) { height_ = height; }

    lv_obj_t *create(lv_obj_t *parent);
    void set_title(const std::string &title);
    void update_time();
    void update_network();
    void update_battery(const cp0_battery_info_t &battery);
    void set_visible(bool visible);
    bool visible() const;
    void set_active(bool active);
    void detach_from_deleted_parent();
    AppTopBarComponent *add_component(std::unique_ptr<AppTopBarComponent> component);
    bool remove_component(const std::string &id);
    AppTopBarComponent *component(const std::string &id) const;
    lv_obj_t *container() const { return container_; }

private:
    friend class AppTopBarRegion;

    void set_delete_callback(std::function<void()> callback)
    {
        delete_callback_ = std::move(callback);
    }
    static void container_delete_cb(lv_event_t *event);
    static void custom_container_delete_cb(lv_event_t *event);
    int height_ = 20;
    bool active_ = false;
    lv_obj_t *container_ = nullptr;
    lv_obj_t *custom_container_ = nullptr;
    std::unique_ptr<AppTopBarTitleComponent> title_;
    std::unique_ptr<AppTopBarNetworkComponent> network_;
    std::unique_ptr<AppTopBarTimeComponent> time_;
    std::unique_ptr<AppTopBarBatteryComponent> battery_;
    std::unordered_map<std::string, std::unique_ptr<AppTopBarComponent>> components_;
    std::function<void()> delete_callback_;
};

/**
 * @brief Connects a page root to its optional top-bar layout and components.
 *
 * AppPageRoot owns one of these regions internally. Most application pages do
 * not need to instantiate it directly.
 */
class AppTopBarRegion
{
public:
    explicit AppTopBarRegion(AppPageRoot &page);
    ~AppTopBarRegion();

    void enable();
    void disable();
    void on_screen_deleted();
    bool enabled() const { return enabled_; }
    void set_page_title(const std::string &title);
    void update_time();
    void update_status();
    void update_battery(const cp0_battery_info_t &battery);
    void update_datetime_status() { update_time(); }
    void update_status_bar() { update_status(); }
    void update_battery_status(const cp0_battery_info_t &battery) { update_battery(battery); }
    AppTopBarComponent *add_component(std::unique_ptr<AppTopBarComponent> component);
    bool remove_component(const std::string &id);
    AppTopBarComponent *component(const std::string &id) const;
    lv_obj_t *container() const { return top_bar_.container(); }

private:
    static void battery_event_cb(lv_event_t *event);
    static void time_timer_cb(lv_timer_t *timer);
    static void status_timer_cb(lv_timer_t *timer);
    void on_top_bar_deleted();

    UIAppTopBar top_bar_;
    AppPageRoot &page_;
    bool enabled_ = false;
    std::unique_ptr<cp0::status::Lifecycle> lifecycle_;
};

/**
 * @brief Standard page layout with a content region and top bar.
 *
 * AppPage is the usual application-facing class. Use this layout directly
 * only when a custom page base is required.
 */
class AppPageLayout : virtual public AppContentRegion
{
public:
    AppPageLayout();
    virtual ~AppPageLayout() = default;
};

/**
 * @brief Standard content layout combined with a bottom-bar region.
 *
 * This is a mixin layout. A concrete page can inherit from it and add its own
 * page-specific UI and behavior.
 */
class AppPageWithBottomBarLayout : virtual public AppContentRegion,
                                   virtual public AppBottomBarRegion
{
public:
    AppPageWithBottomBarLayout();
    virtual ~AppPageWithBottomBarLayout() = default;
};

/**
 * @brief Default application page base.
 *
 * Inherit from AppPage for a normal page. It provides the root screen, the
 * standard content container (ui_APP_Container), and an enabled top bar.
 * For a completely blank screen with no standard layout, inherit from
 * AppPageRoot instead.
 */
class AppPage : public AppPageLayout
{
public:
    AppPage() = default;
    ~AppPage() override = default;
};

/**
 * @brief Load an already-constructed page's screen and configure input focus.
 * @param page Page object whose screen should become active.
 */
void cp0_lvgl_start_app_page(AppPageRoot &page);

/**
 * @brief Construct, start, and return an application page.
 * @tparam PageT Concrete page type derived from AppPageRoot.
 * @param args Arguments forwarded to PageT's constructor.
 * @return Owning pointer to the running page.
 */
template <class PageT, class... Args>
std::unique_ptr<PageT> cp0_lvgl_start_app(Args &&...args)
{
    auto page = std::make_unique<PageT>(std::forward<Args>(args)...);
    cp0_lvgl_start_app_page(*page);
    return page;
}
