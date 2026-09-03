/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_app_page.hpp"
#include "cp0_callback_contract.hpp"
#include "cp0_pointer_lifecycle.hpp"
#include "cp0_status_lifecycle.hpp"
#include "cp0_status_component_contract.hpp"

namespace {

lv_obj_t *release_fallback_screen = nullptr;
lv_display_t *release_fallback_display = nullptr;

void release_fallback_delete_cb(lv_event_t *event)
{
    auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (release_fallback_screen != deleted) return;
    release_fallback_screen = nullptr;
    release_fallback_display = nullptr;
}

lv_obj_t *get_release_fallback_screen(lv_display_t *display)
{
    if (!display) return nullptr;
    lv_obj_t *active = lv_display_get_screen_active(display);
    lv_obj_t *loading = lv_display_get_screen_loading(display);
    lv_obj_t *previous = lv_display_get_screen_prev(display);
    if (release_fallback_screen && release_fallback_display == display &&
        !cp0::display_references_screen(
            release_fallback_screen, active, loading, previous))
        return release_fallback_screen;

    lv_display_t *old_default = lv_display_get_default();
    lv_display_set_default(display);
    release_fallback_screen = lv_obj_create(nullptr);
    lv_display_set_default(old_default);
    if (!release_fallback_screen) {
        release_fallback_display = nullptr;
        return nullptr;
    }
    release_fallback_display = display;
    lv_obj_add_event_cb(release_fallback_screen, release_fallback_delete_cb,
                        LV_EVENT_DELETE, nullptr);
    lv_obj_set_style_bg_color(release_fallback_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(release_fallback_screen, LV_OPA_COVER, 0);
    return release_fallback_screen;
}

} // namespace

UIAppContainer::UIAppContainer(int height)
    : height_(height)
{
}

UIAppContainer::~UIAppContainer()
{
    if (container_)
        lv_obj_remove_event_cb_with_user_data(container_, container_delete_cb, this);
}

void UIAppContainer::set_height(int height)
{
    height_ = height;
    if (container_) lv_obj_set_height(container_, height_);
}

lv_obj_t *UIAppContainer::create(lv_obj_t *parent)
{
    if (!cp0::can_mount_child(parent, container_))
        return container_;
    container_ = lv_obj_create(parent);
    if (!container_) return nullptr;
    lv_obj_add_event_cb(container_, container_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(container_);
    lv_obj_set_width(container_, 320);
    lv_obj_set_height(container_, height_);
    lv_obj_set_pos(container_, 0, 20);
    lv_obj_clear_flag(container_,
                      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                 LV_OBJ_FLAG_SCROLLABLE));
    return container_;
}

void UIAppContainer::container_delete_cb(lv_event_t *event)
{
    if (!cp0::is_direct_event_target(
            lv_event_get_target(event), lv_event_get_current_target(event))) return;
    auto *self = static_cast<UIAppContainer *>(lv_event_get_user_data(event));
    if (self) self->container_ = nullptr;
}

AppPageRoot::AppPageRoot()
{
    auto rollback = cp0::make_rollback_guard([this] {
        top_bar_region_.reset();
        release_root_screen();
        if (input_group_) {
            lv_group_delete(input_group_);
            input_group_ = nullptr;
        }
    });
    create_base_ui();
    create_input_group();
    top_bar_region_ = std::make_unique<AppTopBarRegion>(*this);
    rollback.dismiss();
}

AppPageRoot::~AppPageRoot()
{
    top_bar_region_.reset();
    release_root_screen();
    if (input_group_) lv_group_delete(input_group_);
}

void AppPageRoot::release_root_screen()
{
    if (!root_screen_) return;

    lv_display_t *display = lv_obj_get_display(root_screen_);
    const bool display_owns_transition = display && cp0::display_references_screen(
        root_screen_, lv_display_get_screen_active(display),
        lv_display_get_screen_loading(display),
        lv_display_get_screen_prev(display));
    if (display_owns_transition) {
        lv_obj_t *fallback = get_release_fallback_screen(display);
        if (fallback) {
            lv_screen_load(fallback);
        } else {
            lv_obj_remove_event_cb_with_user_data(
                root_screen_, screen_delete_cb, this);
            root_screen_ = nullptr;
            return;
        }

        if (cp0::display_references_screen(
                root_screen_, lv_display_get_screen_active(display),
                lv_display_get_screen_loading(display),
                lv_display_get_screen_prev(display))) {
            lv_obj_remove_event_cb_with_user_data(
                root_screen_, screen_delete_cb, this);
            root_screen_ = nullptr;
            return;
        }
    }

    if (root_screen_) lv_obj_delete(root_screen_);
}

void AppPageRoot::enable_top_bar() { top_bar_region_->enable(); }
void AppPageRoot::disable_top_bar() { top_bar_region_->disable(); }
bool AppPageRoot::top_bar_enabled() const { return top_bar_region_->enabled(); }
void AppPageRoot::set_page_title(const std::string &title)
{
    page_title_ = title;
    top_bar_region_->set_page_title(title);
}
void AppPageRoot::update_time() { top_bar_region_->update_time(); }
void AppPageRoot::update_status() { top_bar_region_->update_status(); }
void AppPageRoot::update_battery(const cp0_battery_info_t &battery)
{
    top_bar_region_->update_battery(battery);
}
AppTopBarComponent *AppPageRoot::add_top_bar_component(
    std::unique_ptr<AppTopBarComponent> component)
{
    return top_bar_region_->add_component(std::move(component));
}
bool AppPageRoot::remove_top_bar_component(const std::string &id)
{
    return top_bar_region_->remove_component(id);
}
AppTopBarComponent *AppPageRoot::top_bar_component(const std::string &id) const
{
    return top_bar_region_->component(id);
}

void AppPageRoot::clear_content()
{
    if (!root_screen_) return;
    lv_obj_t *top_bar = top_bar_region_ ? top_bar_region_->container() : nullptr;
    uint32_t index = 0;
    while (index < lv_obj_get_child_count(root_screen_)) {
        lv_obj_t *child = lv_obj_get_child(root_screen_, index);
        if (child == top_bar) {
            ++index;
        } else {
            lv_obj_delete(child);
        }
    }
}

void AppPageRoot::create_base_ui()
{
    root_screen_ = lv_obj_create(nullptr);
    if (!root_screen_) return;
    lv_obj_add_event_cb(root_screen_, screen_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_clear_flag(root_screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(root_screen_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(root_screen_, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(root_screen_, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
}

void AppPageRoot::screen_delete_cb(lv_event_t *event)
{
    if (!cp0::is_direct_event_target(
            lv_event_get_target(event), lv_event_get_current_target(event))) return;
    auto *self = static_cast<AppPageRoot *>(lv_event_get_user_data(event));
    if (!self)
        return;
    self->root_screen_ = nullptr;
    if (self->top_bar_region_)
        self->top_bar_region_->on_screen_deleted();
}

void AppPageRoot::create_input_group()
{
    if (!root_screen_) return;
    input_group_ = lv_group_create();
    if (!input_group_) return;
    lv_group_add_obj(input_group_, root_screen_);
}

AppContentRegion::AppContentRegion()
{
    refresh();
}

AppContentRegion::~AppContentRegion()
{
    if (ui_APP_Container)
        lv_obj_remove_event_cb_with_user_data(ui_APP_Container, container_delete_cb, this);
}

void AppContentRegion::container_delete_cb(lv_event_t *event)
{
    if (!cp0::is_direct_event_target(
            lv_event_get_target(event), lv_event_get_current_target(event))) return;
    auto *self = static_cast<AppContentRegion *>(lv_event_get_user_data(event));
    if (self) self->ui_APP_Container = nullptr;
}

void AppContentRegion::refresh()
{
    app_container_.set_height(cp0::app_content_height(has_bottom_bar_));
    if (ui_APP_Container || !root_screen_) return;

    ui_APP_Container = add_bar(app_container_);
    if (ui_APP_Container)
        lv_obj_add_event_cb(ui_APP_Container, container_delete_cb,
                            LV_EVENT_DELETE, this);
}

AppTopBarComponent::AppTopBarComponent(std::string id, int recommended_width, int height)
    : id_(std::move(id)),
      recommended_width_(recommended_width > 0 ? recommended_width : kDefaultRecommendedWidth),
      height_(height < 1 ? 1 : (height > kMaximumHeight ? kMaximumHeight : height))
{
}

AppTopBarComponent::~AppTopBarComponent()
{
    unmount();
}

void AppTopBarComponent::unmount()
{
    if (obj_) lv_obj_delete(obj_);
}

lv_obj_t *AppTopBarComponent::mount(lv_obj_t *parent)
{
    if (!cp0::can_mount_child(parent, obj_) || id_.empty()) return nullptr;
    obj_ = lv_obj_create(parent);
    if (!obj_) return nullptr;
    lv_obj_add_event_cb(obj_, object_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(obj_);
    lv_obj_set_size(obj_, recommended_width_, height_);
    lv_obj_clear_flag(obj_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                       LV_OBJ_FLAG_SCROLLABLE));
    if (!cp0::status::initialize_with_rollback(
            [this] { on_create(obj_); },
            [this] { return resources_ready(); },
            [this] {
                if (obj_) lv_obj_delete(obj_);
            }))
        return nullptr;
    return obj_;
}

void AppTopBarComponent::object_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<AppTopBarComponent *>(lv_event_get_user_data(event));
    auto *object = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (self && self->obj_ == object) {
        cp0::status::release_before_destroy(
            [self] { self->deactivate(); },
            [self] { self->detach_from_deleted_parent(); });
    }
}

void AppTopBarComponent::detach_from_deleted_parent()
{
    obj_ = nullptr;
    on_object_deleted();
}

void AppTopBarComponent::set_visible(bool visible)
{
    if (!obj_) return;
    if (visible) lv_obj_remove_flag(obj_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj_, LV_OBJ_FLAG_HIDDEN);
}

bool AppTopBarComponent::visible() const
{
    return obj_ && !lv_obj_has_flag(obj_, LV_OBJ_FLAG_HIDDEN);
}

void AppTopBarComponent::set_width(int width)
{
    if (obj_) lv_obj_set_width(obj_, width > 0 ? width : recommended_width_);
}

AppTopBarTitleComponent::AppTopBarTitleComponent()
    : AppTopBarComponent("cp0.title", 150)
{
}

AppTopBarTitleComponent::~AppTopBarTitleComponent()
{
    unmount();
}

void AppTopBarTitleComponent::on_create(lv_obj_t *obj)
{
    label_ = lv_label_create(obj);
    if (!label_) return;
    lv_obj_add_event_cb(label_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(label_, LV_PCT(100), LV_PCT(100));
    lv_label_set_long_mode(label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(label_, lv_color_hex(0xCCAA00), 0);
    lv_obj_set_style_text_font(label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(label_, title_.c_str());
}

void AppTopBarTitleComponent::child_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<AppTopBarTitleComponent *>(lv_event_get_user_data(event));
    auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (self) cp0::clear_if_deleted(self->label_, deleted);
}

void AppTopBarTitleComponent::set_title(const std::string &title)
{
    title_ = title;
    if (label_) lv_label_set_text(label_, title_.c_str());
}

void AppTopBarTitleComponent::on_object_deleted()
{
    label_ = nullptr;
}

bool AppTopBarTitleComponent::resources_ready() const
{
    const lv_obj_t *resources[] = {label_};
    return cp0::status::resources_ready(resources, 1);
}

namespace {

void configure_status_component(lv_obj_t *obj, const lv_image_dsc_t *background)
{
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_image_src(obj, background, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t *create_centered_status_label(lv_obj_t *obj)
{
    lv_obj_t *label = lv_label_create(obj);
    if (!label) return nullptr;
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    lv_label_set_text(label, "--");
    return label;
}

} // namespace

AppTopBarTimeComponent::AppTopBarTimeComponent()
    : AppTopBarComponent("cp0.time", 40)
{
}

AppTopBarTimeComponent::~AppTopBarTimeComponent()
{
    unmount();
}

void AppTopBarTimeComponent::on_create(lv_obj_t *obj)
{
    configure_status_component(obj, &cp0_status_time_background);
    label_ = create_centered_status_label(obj);
    if (label_)
        lv_obj_add_event_cb(label_, child_delete_cb, LV_EVENT_DELETE, this);
}

void AppTopBarTimeComponent::child_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<AppTopBarTimeComponent *>(lv_event_get_user_data(event));
    auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (self) cp0::clear_if_deleted(self->label_, deleted);
}

void AppTopBarTimeComponent::refresh()
{
    if (!label_) return;
    char text[16] = "--:--";
    cp0_time_str(text, sizeof(text));
    lv_label_set_text(label_, text[0] ? text : "--:--");
}

void AppTopBarTimeComponent::on_object_deleted()
{
    label_ = nullptr;
}

bool AppTopBarTimeComponent::resources_ready() const
{
    const lv_obj_t *resources[] = {label_};
    return cp0::status::resources_ready(resources, 1);
}

AppTopBarBatteryComponent::AppTopBarBatteryComponent()
    : AppTopBarComponent("cp0.battery", 36)
{
}

AppTopBarBatteryComponent::~AppTopBarBatteryComponent()
{
    deactivate();
    unmount();
}

void AppTopBarBatteryComponent::on_create(lv_obj_t *obj)
{
    configure_status_component(obj, &cp0_status_battery_background);

    battery_bar_ = lv_bar_create(obj);
    if (!battery_bar_) return;
    lv_obj_add_event_cb(battery_bar_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_bar_set_value(battery_bar_, 96, LV_ANIM_OFF);
    lv_bar_set_start_value(battery_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_size(battery_bar_, 33, 14);
    lv_obj_set_align(battery_bar_, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(battery_bar_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(0x484847),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_bar_, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(battery_bar_, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(0x66CC33),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_bar_, LV_OPA_TRANSP,
                            LV_PART_INDICATOR | LV_STATE_DEFAULT);

    charge_wave_ = lv_obj_create(obj);
    if (!charge_wave_) return;
    lv_obj_add_event_cb(charge_wave_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_size(charge_wave_, 8, 14);
    lv_obj_set_pos(charge_wave_, -8, 1);
    lv_obj_clear_flag(charge_wave_, static_cast<lv_obj_flag_t>(
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_radius(charge_wave_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(charge_wave_, lv_color_hex(0xFFF176),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(charge_wave_, 190, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(charge_wave_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(charge_wave_, LV_OBJ_FLAG_HIDDEN);

    label_ = create_centered_status_label(obj);
    if (label_) {
        lv_obj_add_event_cb(label_, child_delete_cb, LV_EVENT_DELETE, this);
        lv_obj_move_foreground(label_);
    }
}

void AppTopBarBatteryComponent::child_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<AppTopBarBatteryComponent *>(lv_event_get_user_data(event));
    auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (!self) return;
    cp0::clear_if_deleted(self->battery_bar_, deleted);
    cp0::clear_if_deleted(self->charge_wave_, deleted);
    cp0::clear_if_deleted(self->label_, deleted);
}

void AppTopBarBatteryComponent::update(const cp0_battery_info_t &battery)
{
    if (!label_) return;
    if (!battery.valid) {
        lv_label_set_text(label_, "--");
        set_charging(false);
        set_low_battery_blink(false);
        return;
    }
    int charge = battery.soc < 0 ? 0 : (battery.soc > 100 ? 100 : battery.soc);
    char text[16];
    std::snprintf(text, sizeof(text), "%d%%", charge);
    lv_label_set_text(label_, text);
    const bool charging = (battery.flags & 1) != 0;
    set_charging(charging);
    set_low_battery_blink(charge < 3);
}

void AppTopBarBatteryComponent::deactivate()
{
    set_charging(false);
    set_low_battery_blink(false);
}

void AppTopBarBatteryComponent::blink_cb(void *object, int32_t opacity)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(opacity), 0);
}

void AppTopBarBatteryComponent::set_charging(bool charging)
{
    if (!obj() || !battery_bar_ || !label_ || !charge_wave_) return;
    lv_obj_set_style_bg_opa(obj(), LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_bar_, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_bar_, LV_OPA_TRANSP,
                            LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_, lv_color_hex(charging ? 0xFFF2A8 : 0xFFFFFF),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    if (charging == charging_) return;
    charging_ = charging;
    if (!charging) {
        lv_anim_del(charge_wave_, nullptr);
        lv_obj_set_x(charge_wave_, -8);
        lv_obj_add_flag(charge_wave_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(charge_wave_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(label_);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, charge_wave_);
    lv_anim_set_values(&animation, -8, 36);
    lv_anim_set_time(&animation, 850);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_set_exec_cb(&animation, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_x));
    lv_anim_start(&animation);
}

void AppTopBarBatteryComponent::set_low_battery_blink(bool enabled)
{
    if (!obj() || enabled == low_battery_blink_) return;
    low_battery_blink_ = enabled;
    lv_anim_del(obj(), blink_cb);
    if (!enabled) {
        lv_obj_set_style_opa(obj(), LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }

    lv_obj_set_style_opa(obj(), LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, obj());
    lv_anim_set_values(&animation, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_time(&animation, 450);
    lv_anim_set_playback_time(&animation, 450);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_set_exec_cb(&animation, blink_cb);
    lv_anim_start(&animation);
}

void AppTopBarBatteryComponent::on_object_deleted()
{
    battery_bar_ = nullptr;
    charge_wave_ = nullptr;
    label_ = nullptr;
    charging_ = false;
    low_battery_blink_ = false;
}

bool AppTopBarBatteryComponent::resources_ready() const
{
    const lv_obj_t *resources[] = {battery_bar_, charge_wave_, label_};
    return cp0::status::resources_ready(resources, 3);
}

AppTopBarNetworkComponent::AppTopBarNetworkComponent()
    : AppTopBarComponent("cp0.network", 28, 15)
{
}

AppTopBarNetworkComponent::~AppTopBarNetworkComponent()
{
    unmount();
}

void AppTopBarNetworkComponent::on_create(lv_obj_t *obj)
{
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 4, 0);

    ethernet_image_ = lv_image_create(obj);
    if (!ethernet_image_) return;
    lv_obj_add_event_cb(ethernet_image_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_image_set_src(ethernet_image_, &cp0_status_ethernet);

    static constexpr int HEIGHTS[] = {6, 9, 12, 15};
    wifi_panel_ = lv_obj_create(obj);
    if (!wifi_panel_) return;
    lv_obj_add_event_cb(wifi_panel_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(wifi_panel_);
    lv_obj_set_size(wifi_panel_, 22, 15);
    for (int index = 0; index < 4; ++index) {
        wifi_bars_[index] = lv_obj_create(wifi_panel_);
        if (!wifi_bars_[index]) return;
        lv_obj_add_event_cb(wifi_bars_[index], child_delete_cb, LV_EVENT_DELETE, this);
        lv_obj_set_size(wifi_bars_[index], 4, HEIGHTS[index]);
        lv_obj_set_pos(wifi_bars_[index], index * 6, 15 - HEIGHTS[index]);
        lv_obj_set_style_radius(wifi_bars_[index], 2, 0);
        lv_obj_set_style_bg_color(wifi_bars_[index], lv_color_hex(0x4D4D4D), 0);
        lv_obj_set_style_border_width(wifi_bars_[index], 0, 0);
        lv_obj_clear_flag(wifi_bars_[index], LV_OBJ_FLAG_SCROLLABLE);
    }
    set_visible(false);
}

void AppTopBarNetworkComponent::child_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<AppTopBarNetworkComponent *>(lv_event_get_user_data(event));
    auto *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (!self) return;
    cp0::clear_if_deleted(self->ethernet_image_, deleted);
    cp0::clear_if_deleted(self->wifi_panel_, deleted);
    for (lv_obj_t *&bar : self->wifi_bars_)
        cp0::clear_if_deleted(bar, deleted);
}

void AppTopBarNetworkComponent::refresh()
{
    if (!obj() || !ethernet_image_ || !wifi_panel_) return;
    for (lv_obj_t *bar : wifi_bars_)
        if (!bar) return;

    cp0_wifi_status_t status{};
    if (cp0_wifi_status_read(&status) != 0) status = {};
    static constexpr int THRESHOLDS[] = {1, 30, 60, 80};
    for (int index = 0; index < 4; ++index) {
        bool active = status.connected && status.signal >= THRESHOLDS[index];
        lv_obj_set_style_bg_color(wifi_bars_[index],
                                  lv_color_hex(active ? 0x33CC33 : 0x4D4D4D), 0);
    }
    if (status.ethernet) lv_obj_remove_flag(ethernet_image_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ethernet_image_, LV_OBJ_FLAG_HIDDEN);
    if (status.connected) lv_obj_remove_flag(wifi_panel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(wifi_panel_, LV_OBJ_FLAG_HIDDEN);
    set_width(status.ethernet && status.connected ? 54 :
              (status.ethernet ? 28 : recommended_width()));
    set_visible(status.connected || status.ethernet);
}

void AppTopBarNetworkComponent::on_object_deleted()
{
    ethernet_image_ = nullptr;
    wifi_panel_ = nullptr;
    for (lv_obj_t *&bar : wifi_bars_) bar = nullptr;
}

bool AppTopBarNetworkComponent::resources_ready() const
{
    const lv_obj_t *resources[] = {
        ethernet_image_, wifi_panel_, wifi_bars_[0], wifi_bars_[1],
        wifi_bars_[2], wifi_bars_[3]};
    return cp0::status::resources_ready(resources, 6);
}

UIAppTopBar::UIAppTopBar()
    : title_(std::make_unique<AppTopBarTitleComponent>()),
      network_(std::make_unique<AppTopBarNetworkComponent>()),
      time_(std::make_unique<AppTopBarTimeComponent>()),
      battery_(std::make_unique<AppTopBarBatteryComponent>())
{
}

UIAppTopBar::~UIAppTopBar()
{
    for (auto &entry : components_) {
        AppTopBarComponent *component = entry.second.get();
        cp0::status::release_before_destroy(
            [component] { component->deactivate(); },
            [component] { component->unmount(); });
    }
    components_.clear();
    set_active(false);
    if (container_)
        lv_obj_remove_event_cb_with_user_data(container_, container_delete_cb, this);
    if (custom_container_)
        lv_obj_remove_event_cb_with_user_data(custom_container_, custom_container_delete_cb, this);
}

lv_obj_t *UIAppTopBar::create(lv_obj_t *parent)
{
    if (!cp0::can_mount_child(parent, container_)) return nullptr;
    container_ = lv_obj_create(parent);
    if (!container_) return nullptr;
    lv_obj_add_event_cb(container_, container_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, 320, height_);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(container_, 5, 0);
    lv_obj_set_style_pad_column(container_, 4, 0);
    lv_obj_clear_flag(container_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                              LV_OBJ_FLAG_SCROLLABLE));

    if (!title_->mount(container_)) {
        lv_obj_delete(container_);
        return nullptr;
    }
    lv_obj_set_width(title_->obj(), 0);
    lv_obj_set_flex_grow(title_->obj(), 1);

    custom_container_ = lv_obj_create(container_);
    if (!custom_container_) {
        lv_obj_delete(container_);
        return nullptr;
    }
    lv_obj_add_event_cb(custom_container_, custom_container_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(custom_container_);
    lv_obj_set_size(custom_container_, LV_SIZE_CONTENT, height_);
    lv_obj_set_flex_flow(custom_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(custom_container_, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(custom_container_, 4, 0);
    lv_obj_clear_flag(custom_container_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                                    LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(custom_container_, LV_OBJ_FLAG_HIDDEN);

    if (!network_->mount(container_) || !time_->mount(container_) ||
        !battery_->mount(container_)) {
        lv_obj_delete(container_);
        return nullptr;
    }
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    return container_;
}

void UIAppTopBar::container_delete_cb(lv_event_t *event)
{
    if (!cp0::is_direct_event_target(
            lv_event_get_target(event), lv_event_get_current_target(event))) return;
    auto *self = static_cast<UIAppTopBar *>(lv_event_get_user_data(event));
    if (self) {
        self->detach_from_deleted_parent();
        cp0::callback::invoke_direct(self->delete_callback_);
    }
}

void UIAppTopBar::custom_container_delete_cb(lv_event_t *event)
{
    if (!cp0::is_direct_event_target(
            lv_event_get_target(event), lv_event_get_current_target(event))) return;
    auto *self = static_cast<UIAppTopBar *>(lv_event_get_user_data(event));
    if (!self) return;
    for (auto &entry : self->components_) {
        entry.second->deactivate();
        entry.second->detach_from_deleted_parent();
    }
    self->custom_container_ = nullptr;
}

void UIAppTopBar::detach_from_deleted_parent()
{
    auto detach = [](AppTopBarComponent *component) {
        cp0::status::release_before_destroy(
            [component] { component->deactivate(); },
            [component] { component->detach_from_deleted_parent(); });
    };
    detach(title_.get());
    detach(network_.get());
    detach(time_.get());
    detach(battery_.get());
    for (auto &entry : components_) detach(entry.second.get());
    container_ = nullptr;
    custom_container_ = nullptr;
    active_ = false;
}

void UIAppTopBar::set_visible(bool visible)
{
    if (!container_) return;
    if (visible) lv_obj_remove_flag(container_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

bool UIAppTopBar::visible() const
{
    return container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void UIAppTopBar::set_active(bool active)
{
    if (active_ == active) return;
    active_ = active;
    auto apply = [active](AppTopBarComponent *component) {
        if (active) {
            if (!component->obj()) return;
            component->activate();
            component->refresh();
        } else {
            component->deactivate();
        }
    };
    apply(title_.get());
    apply(network_.get());
    apply(time_.get());
    apply(battery_.get());
    for (auto &entry : components_) apply(entry.second.get());
}

AppTopBarComponent *UIAppTopBar::add_component(
    std::unique_ptr<AppTopBarComponent> component)
{
    if (!component || component->id().empty() || !custom_container_) return nullptr;
    const std::string id = component->id();
    if (id == title_->id() || id == network_->id() ||
        id == time_->id() || id == battery_->id()) return nullptr;
    remove_component(id);
    if (!component->mount(custom_container_)) return nullptr;
    AppTopBarComponent *result = component.get();
    components_[id] = std::move(component);
    if (active_) {
        result->activate();
        result->refresh();
    }
    lv_obj_remove_flag(custom_container_, LV_OBJ_FLAG_HIDDEN);
    return result;
}

bool UIAppTopBar::remove_component(const std::string &id)
{
    const auto found = components_.find(id);
    if (found == components_.end()) return false;
    AppTopBarComponent *component = found->second.get();
    cp0::status::release_before_destroy(
        [component] { component->deactivate(); },
        [component] { component->unmount(); });
    components_.erase(found);
    if (components_.empty() && custom_container_)
        lv_obj_add_flag(custom_container_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

AppTopBarComponent *UIAppTopBar::component(const std::string &id) const
{
    if (id == title_->id()) return title_.get();
    if (id == network_->id()) return network_.get();
    if (id == time_->id()) return time_.get();
    if (id == battery_->id()) return battery_.get();
    const auto found = components_.find(id);
    return found == components_.end() ? nullptr : found->second.get();
}

void UIAppTopBar::set_title(const std::string &title)
{
    if (!container_) return;
    title_->set_title(title);
}

void UIAppTopBar::update_time()
{
    if (!container_) return;
    time_->refresh();
}

void UIAppTopBar::update_network()
{
    if (!container_) return;
    network_->refresh();
}

void UIAppTopBar::update_battery(const cp0_battery_info_t &battery)
{
    if (!container_) return;
    battery_->update(battery);
}

AppTopBarRegion::AppTopBarRegion(AppPageRoot &page)
    : page_(page), lifecycle_(std::make_unique<cp0::status::Lifecycle>())
{
    top_bar_.set_delete_callback([this] { on_top_bar_deleted(); });
    top_bar_.set_height(page_.top_bar_height_px_);
    top_bar_.set_title(page_.page_title_);
    top_bar_.create(page_.screen());
}

void AppTopBarRegion::on_top_bar_deleted()
{
    enabled_ = false;
    lifecycle_->stop();
}

void AppTopBarRegion::enable()
{
    if (enabled_) return;
    if (!page_.screen() || !top_bar_.container()) return;
    top_bar_.set_visible(true);
    top_bar_.set_active(true);
    update_time();
    update_status();
    update_battery(cp0_battery_read());
    enabled_ = lifecycle_->start({
        [this] {
            const uint32_t event_code = lv_c_event[CP0_C_EVENT_BATTERY];
            lv_obj_t *screen = page_.screen();
            if (event_code == 0)
                return cp0::status::Resource{true, {}};
            if (!screen)
                return cp0::status::Resource{};
            lv_obj_add_event_cb(screen, battery_event_cb,
                                static_cast<lv_event_code_t>(event_code), this);
            return cp0::status::Resource{
                true,
                [this, screen] {
                    if (page_.screen() == screen)
                        lv_obj_remove_event_cb_with_user_data(screen, battery_event_cb, this);
                }};
        },
        [this] {
            lv_timer_t *timer = lv_timer_create(time_timer_cb, 1000, this);
            return cp0::status::Resource{
                timer != nullptr, [timer] { lv_timer_delete(timer); }};
        },
        [this] {
            lv_timer_t *timer = lv_timer_create(status_timer_cb, 5000, this);
            return cp0::status::Resource{
                timer != nullptr, [timer] { lv_timer_delete(timer); }};
        },
    });
    if (!enabled_) {
        top_bar_.set_active(false);
        top_bar_.set_visible(false);
    }
}

void AppTopBarRegion::disable()
{
    if (!enabled_) return;
    enabled_ = false;
    lifecycle_->stop();
    top_bar_.set_active(false);
    top_bar_.set_visible(false);
}

void AppTopBarRegion::on_screen_deleted()
{
    enabled_ = false;
    cp0::status::release_before_destroy(
        [this] { lifecycle_->stop(); },
        [this] { top_bar_.detach_from_deleted_parent(); });
}

AppTopBarRegion::~AppTopBarRegion()
{
    disable();
}

void AppTopBarRegion::set_page_title(const std::string &title)
{
    top_bar_.set_title(title);
}

void AppTopBarRegion::update_time() { top_bar_.update_time(); }
void AppTopBarRegion::update_status() { top_bar_.update_network(); }
void AppTopBarRegion::update_battery(const cp0_battery_info_t &battery)
{
    top_bar_.update_battery(battery);
}

AppTopBarComponent *AppTopBarRegion::add_component(
    std::unique_ptr<AppTopBarComponent> component)
{
    return top_bar_.add_component(std::move(component));
}

bool AppTopBarRegion::remove_component(const std::string &id)
{
    return top_bar_.remove_component(id);
}

AppTopBarComponent *AppTopBarRegion::component(const std::string &id) const
{
    return top_bar_.component(id);
}

void AppTopBarRegion::battery_event_cb(lv_event_t *event)
{
    auto *self = static_cast<AppTopBarRegion *>(lv_event_get_user_data(event));
    auto *battery = static_cast<const cp0_battery_info_t *>(lv_event_get_param(event));
    if (!battery) return;
    cp0::callback::invoke_if_present(
        self, [battery](AppTopBarRegion &region) { region.update_battery(*battery); });
}

void AppTopBarRegion::time_timer_cb(lv_timer_t *timer)
{
    auto *self = static_cast<AppTopBarRegion *>(lv_timer_get_user_data(timer));
    cp0::callback::invoke_if_present(
        self, [](AppTopBarRegion &region) { region.update_time(); });
}

void AppTopBarRegion::status_timer_cb(lv_timer_t *timer)
{
    auto *self = static_cast<AppTopBarRegion *>(lv_timer_get_user_data(timer));
    cp0::callback::invoke_if_present(
        self, [](AppTopBarRegion &region) { region.update_status(); });
}

AppBottomBarRegion::AppBottomBarRegion()
{
    has_bottom_bar_ = true;
    refresh();
    if (!root_screen_) {
        has_bottom_bar_ = false;
        refresh();
        return;
    }
    ui_BOTTOM_Container = lv_obj_create(root_screen_);
    if (!ui_BOTTOM_Container) {
        has_bottom_bar_ = false;
        refresh();
        return;
    }
    lv_obj_add_event_cb(ui_BOTTOM_Container, container_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(ui_BOTTOM_Container);
    lv_obj_set_size(ui_BOTTOM_Container, 320, 20);
    lv_obj_set_pos(ui_BOTTOM_Container, 0, 150);
    lv_obj_clear_flag(ui_BOTTOM_Container,
                      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                 LV_OBJ_FLAG_SCROLLABLE));
}

AppBottomBarRegion::~AppBottomBarRegion()
{
    if (ui_BOTTOM_Container)
        lv_obj_remove_event_cb_with_user_data(ui_BOTTOM_Container, container_delete_cb, this);
}

void AppBottomBarRegion::container_delete_cb(lv_event_t *event)
{
    if (!cp0::is_direct_event_target(
            lv_event_get_target(event), lv_event_get_current_target(event))) return;
    auto *self = static_cast<AppBottomBarRegion *>(lv_event_get_user_data(event));
    if (!self) return;
    self->ui_BOTTOM_Container = nullptr;
    self->has_bottom_bar_ = false;
    if (self->root_screen_) self->refresh();
}

AppPageLayout::AppPageLayout()
    : AppPageRoot(), AppContentRegion()
{
    enable_top_bar();
}

AppPageWithBottomBarLayout::AppPageWithBottomBarLayout()
    : AppPageRoot(), AppContentRegion(), AppBottomBarRegion()
{
    enable_top_bar();
}

void cp0_lvgl_start_app_page(AppPageRoot &page)
{
    if (!page.screen()) return;
    lv_screen_load(page.screen());
    if (page.input_group()) {
        lv_group_set_default(page.input_group());
        lv_indev_t *input = lv_indev_get_next(nullptr);
        while (input) {
            const lv_indev_type_t type = lv_indev_get_type(input);
            if (cp0::input_type_uses_group(
                    type, LV_INDEV_TYPE_KEYPAD, LV_INDEV_TYPE_ENCODER))
                lv_indev_set_group(input, page.input_group());
            input = lv_indev_get_next(input);
        }
    }
    lv_obj_invalidate(page.screen());
    lv_refr_now(nullptr);
}
