#pragma once

#include "hal_lvgl_bsp.h"
#include "settings_camera_resolution_adapter.hpp"

#include <cstdint>
#include <functional>

#include "lvgl_components.hpp"

namespace camera_resolution_settings = settings_camera_resolution;

class LvSettingResolutionPage3 : public LvSettingValuePage3Base {
public:
    LvSettingResolutionPage3();

    LvSettingResolutionPage3(lv_obj_t *parent, const NodeIter &parent_node);

    LvSettingResolutionPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback);

    ~LvSettingResolutionPage3() override;

protected:
    int initial_selection() const override;

    SettingApiResult activate_selected() override;

private:
    static camera_resolution_settings::ConfigInvoker config_invoker();

    bool request_is_current(std::uint64_t generation) const;

    void restore_focus();

    void create_status_label();

    void set_status(const char *text, bool error);

    void finish_load(const camera_resolution_settings::ReadResult &result);

    void finish_load_failure();

    void begin_load();

    void finish_write(const camera_resolution_settings::WriteResult &result);

    void finish_write_failure();

    bool begin_write();

    bool page_alive_ = true;
    bool pending_ = false;
    bool loaded_ = false;
    std::uint64_t generation_ = 0;
    camera_resolution_settings::Resolution saved_resolution_ = camera_resolution_settings::kDefaultResolution;
    camera_resolution_settings::Resolution selected_resolution_ = camera_resolution_settings::kDefaultResolution;
    int saved_index_ = 0;
    lv_obj_t *status_label_ = nullptr;
};
