#pragma once

#include <ctime>
#include <functional>
#include <utility>

#include "lvgl_componens.hpp"

class LvSettingRtcPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRtcPage3() = default;

    LvSettingRtcPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        initialize(parent);
    }

    LvSettingRtcPage3(lv_obj_t *parent,
                      const NodeIter &parent_node,
                      std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        initialize(parent);
    }

protected:
    int initial_selection() const override
    {
        const std::string &label = parent_node()->label;
        const std::time_t now = std::time(nullptr);
        const std::tm *local = std::localtime(&now);
        if (!local) return 0;
        if (label == "Year") return local->tm_year + 1900 - 2000;
        if (label == "Month") return local->tm_mon;
        if (label == "Day") return local->tm_mday - 1;
        if (label == "Hour") return local->tm_hour;
        if (label == "Minute") return local->tm_min;
        if (label == "Second") return local->tm_sec;
        return 0;
    }
};
