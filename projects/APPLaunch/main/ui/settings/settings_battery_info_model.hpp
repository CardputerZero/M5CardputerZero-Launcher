#pragma once

#include <array>
#include <cstddef>
#include <string>

struct SettingsBatterySnapshot {
    int voltage_mv = 0;
    int current_ma = 0;
    int temperature_c10 = 0;
    int soc = 0;
    int remain_mah = 0;
    int full_mah = 0;
    int flags = 0;
    int avg_current_ma = 0;
    bool valid = false;
};

enum class SettingsBatteryReadState {
    Invalid,
    Valid,
};

class SettingsBatteryInfoModel {
public:
    enum class LabelMetric : std::size_t {
        Count = 6,
    };

    bool update(int result_code, const std::string &response);
    void set_status(const std::string &status);
    void invalidate(const std::string &reason = "Battery unavailable");

    const SettingsBatterySnapshot &snapshot() const { return snapshot_; }
    const std::array<std::string, static_cast<std::size_t>(LabelMetric::Count)> &labels() const
    {
        return labels_;
    }
    SettingsBatteryReadState state() const { return state_; }
    bool valid() const { return state_ == SettingsBatteryReadState::Valid; }
    const std::string &status_text() const { return status_text_; }

    static bool parse_payload(const std::string &response,
                              SettingsBatterySnapshot &snapshot);

private:
    void rebuild_labels();

    SettingsBatterySnapshot snapshot_;
    SettingsBatteryReadState state_ = SettingsBatteryReadState::Invalid;
    std::string status_text_ = "Battery unavailable";
    std::array<std::string, static_cast<std::size_t>(LabelMetric::Count)> labels_ = {
        "Battery: --%",
        "Temp: --C",
        "Current: --mA",
        "Voltage: --V",
        "Remaining: --mAh",
        "Full: --mAh",
    };
};
