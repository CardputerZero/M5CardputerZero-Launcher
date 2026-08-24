#include "../main/ui/model/setup_info_model.hpp"
#include "../main/ui/model/setup_info_timer_contract.hpp"

#include <cassert>
#include <cstdint>

int main()
{
    int timer = 1;
    int page = 3;
    assert(setup_info_timer_callback_ready(&timer, true, &page));
    assert(!setup_info_timer_callback_ready(&timer, false, &page));
    assert(!setup_info_timer_callback_ready(
        &timer, true, static_cast<int *>(nullptr)));

    int visible_label = 1;
    int bubbled_label = 2;
    static_assert(noexcept(setup_info_label_delete_callback_allowed(
        &visible_label, &visible_label)));
    assert(setup_info_label_delete_callback_allowed(
        &visible_label, &visible_label));
    assert(!setup_info_label_delete_callback_allowed(
        &visible_label, &bubbled_label));
    assert(!setup_info_label_delete_callback_allowed(
        static_cast<int *>(nullptr), &visible_label));

    SetupInfoModel model;
    assert(!model.update(-1, ""));
    assert(model.labels()[0] == "Battery: --%");
    assert(!model.update(0, "broken"));
    assert(!model.update(0, "4012,0,253,87,1000,1200,0,-100,1junk"));
    assert(!model.update(0, "4012,0,253,87,1000,1200,0,-100,1,trailing"));
    assert(!model.update(0, " 4012,0,253,87,1000,1200,0,-100,1"));
    assert(!model.update(0, "4000,10,250,101,1,2,3,4,1"));
    assert(!model.update(0, "4000,10,250,90,1,2,3,4,2"));
    assert(!model.update(0, "4000,10,1001,90,1,2,3,4,1"));
    assert(!model.update(0, "4000,5001,250,90,1,2,3,4,1"));
    assert(!model.update(0, "4000,10,250,90,3,2,3,4,1"));
    assert(!model.update(0, "999999999999999999999,10,250,90,1,2,3,4,1"));

    assert(model.update(0, "4012,-125,253,87,1000,1200,0,-100,1"));
    assert(model.labels()[0] == "Battery: 87%");
    assert(model.labels()[1] == "Temp: 25.3C");
    assert(model.labels()[2] == "Current: -125mA");
    assert(model.labels()[3] == "Voltage: 4.01V");

    assert(model.update(0, "4012,-2147483648,253,87,1000,1200,0,-100,1"));
    assert(model.labels()[2] == "Current: --mA");
    const auto stable_snapshot = model.snapshot();
    const auto stable_labels = model.labels();
    assert(!model.update(-1, ""));
    assert(!model.update(0, "broken"));
    assert(!model.update(0, "4012,0,253,87,1000,1200,0,-100,0"));
    assert(model.snapshot().valid);
    assert(model.snapshot().voltage_mv == stable_snapshot.voltage_mv);
    assert(model.snapshot().current_ma == stable_snapshot.current_ma);
    assert(model.labels() == stable_labels);
    assert(model.labels()[0] == "Battery: 87%");
    assert(model.labels()[1] == "Temp: 25.3C");
    assert(model.labels()[2] == "Current: --mA");
    assert(model.labels()[3] == "Voltage: 4.01V");
    return 0;
}
