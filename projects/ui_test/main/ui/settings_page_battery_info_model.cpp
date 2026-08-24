#include "settings_battery_info_model.hpp"

#include <array>
#include <charconv>
#include <climits>
#include <cstdio>

namespace {

bool parse_fields(const std::string &response, std::array<int, 9> &fields)
{
    std::size_t begin = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const std::size_t end = index + 1 == fields.size()
                                    ? response.size()
                                    : response.find(',', begin);
        if (end == std::string::npos || end == begin) return false;

        const char *first = response.data() + begin;
        const char *last = response.data() + end;
        const auto parsed = std::from_chars(first, last, fields[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != last) return false;
        begin = end + (index + 1 == fields.size() ? 0 : 1);
    }
    return begin == response.size();
}

bool valid_current(int value)
{
    return value == INT_MIN || (value >= -5000 && value <= 5000);
}

bool valid_average_current(int value)
{
    return value >= -5000 && value <= 5000;
}

bool valid_snapshot(const SettingsBatterySnapshot &snapshot)
{
    return snapshot.valid && snapshot.voltage_mv >= 0 && snapshot.voltage_mv <= 20000 &&
           valid_current(snapshot.current_ma) &&
           snapshot.temperature_c10 >= -400 && snapshot.temperature_c10 <= 1000 &&
           snapshot.soc >= 0 && snapshot.soc <= 100 && snapshot.remain_mah >= 0 &&
           snapshot.full_mah >= 0 &&
           (snapshot.full_mah == 0 || snapshot.remain_mah <= snapshot.full_mah) &&
           snapshot.flags >= 0 && valid_average_current(snapshot.avg_current_ma);
}

} // namespace

bool SettingsBatteryInfoModel::parse_payload(const std::string &response,
                                             SettingsBatterySnapshot &snapshot)
{
    std::array<int, 9> fields{};
    if (!parse_fields(response, fields)) return false;

    SettingsBatterySnapshot parsed;
    parsed.voltage_mv = fields[0];
    parsed.current_ma = fields[1];
    parsed.temperature_c10 = fields[2];
    parsed.soc = fields[3];
    parsed.remain_mah = fields[4];
    parsed.full_mah = fields[5];
    parsed.flags = fields[6];
    parsed.avg_current_ma = fields[7];
    parsed.valid = fields[8] == 1;
    if (!valid_snapshot(parsed)) return false;

    snapshot = parsed;
    return true;
}

bool SettingsBatteryInfoModel::update(int result_code, const std::string &response)
{
    invalidate(result_code == 0 ? "Invalid battery data" : "Battery read failed");
    if (result_code != 0) return false;

    SettingsBatterySnapshot parsed;
    if (!parse_payload(response, parsed)) return false;

    snapshot_ = parsed;
    state_ = SettingsBatteryReadState::Valid;
    status_text_ = "Battery updated";
    rebuild_labels();
    return true;
}

void SettingsBatteryInfoModel::invalidate(const std::string &reason)
{
    snapshot_ = {};
    state_ = SettingsBatteryReadState::Invalid;
    status_text_ = reason.empty() ? "Battery unavailable" : reason;
    rebuild_labels();
}

void SettingsBatteryInfoModel::rebuild_labels()
{
    if (!valid()) {
        labels_ = {
            "Battery: --%",
            "Temp: --C",
            "Current: --mA",
            "Voltage: --V",
            "Remaining: --mAh",
            "Full: --mAh",
        };
        return;
    }

    char text[64];
    std::snprintf(text, sizeof(text), "Battery: %d%%", snapshot_.soc);
    labels_[0] = text;
    std::snprintf(text, sizeof(text), "Temp: %.1fC", snapshot_.temperature_c10 / 10.0);
    labels_[1] = text;
    if (snapshot_.current_ma == INT_MIN)
        labels_[2] = "Current: --mA";
    else {
        std::snprintf(text, sizeof(text), "Current: %dmA", snapshot_.current_ma);
        labels_[2] = text;
    }
    std::snprintf(text, sizeof(text), "Voltage: %.2fV", snapshot_.voltage_mv / 1000.0);
    labels_[3] = text;
    std::snprintf(text, sizeof(text), "Remaining: %dmAh", snapshot_.remain_mah);
    labels_[4] = text;
    std::snprintf(text, sizeof(text), "Full: %dmAh", snapshot_.full_mah);
    labels_[5] = text;
}
