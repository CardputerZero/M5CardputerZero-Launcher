#pragma once

#include "lvgl/lvgl.h"
#include "zclaw_panel_lifecycle.h"

#include <string>

namespace zclaw {

class FontManager;

class SettingsPanel {
public:
    static constexpr int kMaximumRows = 6;

    SettingsPanel() = default;
    ~SettingsPanel();

    SettingsPanel(const SettingsPanel &) = delete;
    SettingsPanel &operator=(const SettingsPanel &) = delete;

    bool create(lv_obj_t *parent, const FontManager *fonts);
    void show();
    void close();

    bool is_open() const;
    bool is_animating() const;
    lv_obj_t *content() const;

    void set_header(const std::string &title, const std::string &hint);
    void clear_rows();
    bool add_row(const std::string &title, const std::string &value);
    int row_count() const;
    void update_selection(int selected_index);

private:
    static void animation_completed(lv_anim_t *animation);
    static void panel_deleted(lv_event_t *event);
    void on_animation_completed();
    void animate(lv_coord_t from_x, lv_coord_t to_x);
    void destroy_panel();
    void release_panel();

    const FontManager *fonts_ = nullptr;
    PanelLifecycle lifecycle_;
    lv_obj_t *panel_ = nullptr;
    lv_obj_t *header_label_ = nullptr;
    lv_obj_t *hint_label_ = nullptr;
    lv_obj_t *rows_container_ = nullptr;
    lv_obj_t *rows_[kMaximumRows] = {};
    lv_obj_t *values_[kMaximumRows] = {};
    int row_count_ = 0;
};

}  // namespace zclaw
