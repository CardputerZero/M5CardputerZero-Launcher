#include "setup_info_model.hpp"

#include <array>
#include <charconv>
#include <cstdint>
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

} // namespace

bool SetupInfoModel::update(int result_code, const std::string &response)
{
    std::array<int, 9> fields{};
    SetupBatterySnapshot parsed;
    const bool decoded = result_code == 0 && parse_fields(response, fields);
    if (!decoded) return false;
    parsed.voltage_mv = fields[0];
    parsed.current_ma = fields[1];
    parsed.temperature_c10 = fields[2];
    parsed.soc = fields[3];
    const bool current_valid = parsed.current_ma == INT32_MIN ||
                               (parsed.current_ma >= -5000 && parsed.current_ma <= 5000);
    parsed.valid = fields[8] == 1 && parsed.voltage_mv >= 0 &&
                   parsed.voltage_mv <= 20000 && current_valid &&
                   parsed.temperature_c10 >= -400 && parsed.temperature_c10 <= 1000 &&
                   parsed.soc >= 0 && parsed.soc <= 100 && fields[4] >= 0 &&
                   fields[5] >= 0 && (fields[5] == 0 || fields[4] <= fields[5]) &&
                   fields[6] >= 0 && fields[7] >= -5000 && fields[7] <= 5000;
    if (!parsed.valid) return false;
    snapshot_ = parsed;
    rebuild_labels();
    return true;
}

void SetupInfoModel::rebuild_labels()
{
    if (!snapshot_.valid) {
        labels_ = {"Battery: --%", "Temp: --C", "Current: --mA", "Voltage: --V"};
        return;
    }

    char text[64];
    std::snprintf(text, sizeof(text), "Battery: %d%%", snapshot_.soc);
    labels_[0] = text;
    std::snprintf(text, sizeof(text), "Temp: %.1fC", snapshot_.temperature_c10 / 10.0);
    labels_[1] = text;
    if (snapshot_.current_ma == INT32_MIN)
        labels_[2] = "Current: --mA";
    else {
        std::snprintf(text, sizeof(text), "Current: %dmA", snapshot_.current_ma);
        labels_[2] = text;
    }
    std::snprintf(text, sizeof(text), "Voltage: %.2fV", snapshot_.voltage_mv / 1000.0);
    labels_[3] = text;
}
