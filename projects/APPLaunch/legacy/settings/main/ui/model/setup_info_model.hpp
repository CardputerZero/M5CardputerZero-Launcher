#pragma once

#include <array>
#include <string>

struct SetupBatterySnapshot
{
    int voltage_mv = 0;
    int current_ma = 0;
    int temperature_c10 = 0;
    int soc = 0;
    bool valid = false;
};

class SetupInfoModel
{
public:
    bool update(int result_code, const std::string &response);
    const SetupBatterySnapshot &snapshot() const { return snapshot_; }
    const std::array<std::string, 4> &labels() const { return labels_; }

private:
    void rebuild_labels();

    SetupBatterySnapshot snapshot_;
    std::array<std::string, 4> labels_ = {
        "Battery: --%", "Temp: --C", "Current: --mA", "Voltage: --V"};
};
