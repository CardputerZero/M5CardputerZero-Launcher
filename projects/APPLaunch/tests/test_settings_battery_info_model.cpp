#include "../main/ui/settings/settings_battery_info_model.hpp"

#include <array>
#include <cassert>
#include <string>

int main()
{
    SettingsBatteryInfoModel model;
    assert(!model.valid());
    assert(model.labels()[0] == "Battery: --%");
    assert(model.values()[0] == "--%");
    assert(model.values()[4] == "1750 mAh");
    assert(model.values()[5] == "1750 mAh");

    const std::string valid_payload = "3800,-250,250,78,1200,2000,1,-100,1";
    assert(model.update(0, valid_payload));
    const auto previous_snapshot = model.snapshot();
    const auto previous_labels = model.labels();
    const auto previous_values = model.values();
    assert(previous_values[0] == "78%");
    assert(previous_values[1] == "25.0 C");
    assert(previous_values[2] == "-250 mA");
    assert(previous_values[3] == "3.80 V");
    assert(previous_values[4] == "1750 mAh");
    assert(previous_values[5] == "1750 mAh");

    assert(!model.update(-1, "battery api failed"));
    assert(model.valid());
    assert(model.snapshot().voltage_mv == previous_snapshot.voltage_mv);
    assert(model.labels() == previous_labels);
    assert(model.values() == previous_values);
    assert(model.status_text() == "Battery read failed");

    assert(!model.update(0, "invalid payload"));
    assert(model.valid());
    assert(model.labels() == previous_labels);
    assert(model.values() == previous_values);
    assert(model.status_text() == "Invalid battery data");

    model.set_status("Reading battery...");
    assert(model.valid());
    assert(model.labels() == previous_labels);
    assert(model.values() == previous_values);

    model.invalidate();
    assert(!model.valid());
    assert(model.labels()[0] == "Battery: --%");
    assert(model.values()[0] == "--%");
    assert(model.values()[4] == "1750 mAh");
    assert(model.values()[5] == "1750 mAh");
}
