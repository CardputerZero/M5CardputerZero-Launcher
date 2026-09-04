#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "cp0_enum_cast.h"
#include "lvgl_components.hpp"

class LvSettingBQCalibratePage3 : public LvSettingValuePage3Base {
public:
    enum class LayoutMetric : int {
        ResultX = 8,
        ResultY = 118,
        ResultW = 304,
        ResultH = 14,
        FooterH = 32,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return CP0_ENUM_CAST_INT(value);
    }

    LvSettingBQCalibratePage3();

    LvSettingBQCalibratePage3(
        lv_obj_t *parent,
        const NodeIter &parent_node);

    LvSettingBQCalibratePage3(
        lv_obj_t *parent,
        const NodeIter &parent_node,
        std::function<void()> back_callback);

    ~LvSettingBQCalibratePage3() override;

    void create_ui(lv_obj_t *parent) override;

protected:
    int initial_selection() const override;

    SettingApiResult activate_selected() override;

private:
    bool post_to_lvgl(std::function<void()> task);

    void set_result(const std::string &text);

    void handle_result(int outcome, int code, std::uint64_t generation);

    struct State;
    std::unique_ptr<State> state_;
    lv_obj_t *result_label_ = nullptr;
};
