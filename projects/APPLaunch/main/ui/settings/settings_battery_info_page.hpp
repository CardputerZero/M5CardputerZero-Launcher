#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "settings_tree_types.hpp"

#define LVGL_COMPONENTS_ROLLER1_ONLY
#include "lvgl_components.hpp"
#undef LVGL_COMPONENTS_ROLLER1_ONLY

class LvSettingBatteryInfoPage3 : public DComponens::LvglComponensBase {
public:
    enum class LayoutMetric : int {
        ScreenW            = 320,
        ScreenH            = 150,
        TitleX             = 8,
        TitleY             = 2,
        TitleW             = 304,
        TitleH             = 19,
        TableX             = 8,
        TableY             = 23,
        TableW             = 304,
        TableCellW         = 152,
        TableRowH          = 27,
        TableKeyX          = 7,
        TableKeyW          = 70,
        TableValueX        = 77,
        TableValueW        = 72,
        TableTextY         = 4,
        TableTextH         = 18,
        StatusX            = 8,
        StatusY            = 108,
        StatusW            = 304,
        StatusH            = 14,
        HintX              = 8,
        HintY              = 135,
        HintW              = 304,
        HintH              = 13,
        RefreshIntervalMs  = 1000,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingBatteryInfoPage3();
    LvSettingBatteryInfoPage3(
        lv_obj_t *parent,
        const NodeIter &parent_node,
        std::function<void()> back_callback);
    ~LvSettingBatteryInfoPage3() override;

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;
    void create_ui(lv_obj_t *parent) override;

private:
    struct State;

    bool post_to_lvgl(std::function<void()> task);
    static void refresh_timer_cb(lv_timer_t *timer);
    lv_obj_t *create_label(int x,
                           int y,
                           int width,
                           int height,
                           std::uint32_t color,
                           int font_size);
    lv_obj_t *create_table_cell(int row, int column, const char *key);
    void request_read();
    void handle_read_result(int outcome, int code, std::uint64_t generation,
                            const std::string &payload);
    void render();
    void stop_refresh_timer();
    void handle_key_event(lv_event_t *event);

    std::unique_ptr<State> state_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_battery_info_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> on_back);
