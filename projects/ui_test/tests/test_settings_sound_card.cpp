#include "settings_sound_card_adapter.hpp"

#include <cassert>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using ui_test_soundcard::Control;
using ui_test_soundcard::SoundCardApiAdapter;
using ui_test_soundcard::SoundCardModel;

void test_card_and_control_payloads()
{
    const auto cards = SoundCardModel::parse_cards(
        "0\tCard 0: Built-in Audio\n"
        "invalid\tignored\n"
        "2\tCard 2: USB Audio\n");
    assert(cards.size() == 2);
    assert(cards[0].index == 0 && cards[0].label == "Card 0: Built-in Audio");
    assert(cards[1].index == 2 && cards[1].label == "Card 2: USB Audio");

    const auto controls = SoundCardModel::parse_controls(
        "Master\tINTEGER\t0\t100\t1\tFront Left: Playback 45 [45%]\t45\n"
        "Input Source\tENUMERATED\t0\t0\t1\tItem0: 'Mic'\t0\n");
    assert(controls.size() == 2);
    assert(controls[0].current_value == 45);
    assert(controls[1].type == "ENUMERATED");
    assert(controls[1].current_option == "Mic");
}

void test_detail_and_range_rules()
{
    const Control fallback{"Master", "INTEGER", 5, 95, 1,
                           "Playback: 20 [20%]", 20};
    const auto detail = SoundCardModel::parse_detail(
        "  Capabilities: pvolume\n"
        "  Limits: Playback 0 - 100\n"
        "  Front Left: Playback 67 [67%]\n",
        fallback);
    assert(detail.name == "Master");
    assert(detail.type == "INTEGER");
    assert(detail.minimum == 0 && detail.maximum == 100);
    assert(detail.current_value == 67);
    assert(detail.writable);
    assert(SoundCardModel::clamp_value(-1, fallback) == 5);
    assert(SoundCardModel::clamp_value(50, fallback) == 50);
    assert(SoundCardModel::clamp_value(120, fallback) == 95);

    const auto overflow_detail = SoundCardModel::parse_detail(
        "Capabilities: pvolume\n"
        "Limits: Playback 0 - 100\n"
        "Front Left: Playback 999999999999 [100%]\n",
        fallback);
    assert(overflow_detail.current_value == 0);

    const auto read_only = SoundCardModel::parse_detail(
        "Capabilities: inactive\n"
        "Mono: Playback 1 [1%]\n",
        fallback);
    assert(!read_only.writable);

    const auto enumerated = SoundCardModel::parse_detail(
        "Capabilities: enum\n"
        "Items: 'Mic' 'Line'\n"
        "Item0: 'Mic'\n",
        Control{"Input Source", "ENUMERATED", 0, 0, 1, "Item0: 'Mic'", 0});
    assert(enumerated.type == "ENUMERATED");
    assert(enumerated.writable);
    assert(enumerated.options.size() == 2);
    assert(enumerated.current_option == "Mic");

    const Control fallback_enum{"Input Source", "ENUMERATED", 0, 0, 1,
                                "Item0: 'Line'", 0};
    const auto fallback_detail = SoundCardModel::parse_detail(
        "Capabilities: enum\n", fallback_enum);
    assert(fallback_detail.type == "ENUMERATED");
    assert(fallback_detail.current_text.empty());
    assert(fallback_detail.options == fallback_enum.options);
}

void test_malformed_control_rows_are_ignored()
{
    const auto controls = SoundCardModel::parse_controls(
        "Master\tINTEGER\t0\t100\t1\tPlayback: 45\t45\n"
        "\tINTEGER\t0\t100\t1\tPlayback: 1\t1\n"
        "bad-min\tINTEGER\t0junk\t100\t1\tPlayback: 1\t1\n"
        "bad-max\tINTEGER\t0\t2147483648\t1\tPlayback: 1\t1\n"
        "bad-current\tINTEGER\t0\t100\t1\tPlayback: 1\t999999999999\n");
    assert(controls.size() == 1);
    assert(controls[0].name == "Master");
}

void test_detail_payload_validation_accepts_all_channels()
{
    assert(SoundCardModel::has_detail_payload(
        "Capabilities: pvolume\n"
        "Rear Right: Playback 25 [25%]\n"));
    assert(SoundCardModel::has_detail_payload(
        "Capabilities: enum\n"
        "Items: 'Mic' 'Line'\n"
        "Item1: 'Line'\n"));
    assert(!SoundCardModel::has_detail_payload("backend garbage\n"));
}

void test_adapter_queues_results_on_drain_thread()
{
    std::mutex calls_mutex;
    std::vector<std::vector<std::string>> calls;
    SoundCardApiAdapter adapter(
        [&](std::list<std::string> arguments, SoundCardApiAdapter::Completion callback) {
            std::vector<std::string> call(arguments.begin(), arguments.end());
            {
                std::lock_guard<std::mutex> lock(calls_mutex);
                calls.push_back(call);
            }
            callback(0, "payload");
            callback(0, "duplicate");
        });

    bool handled = false;
    std::thread::id handler_thread;
    assert(adapter.request({"ListCards"}, 7,
                           [&](int code, std::string payload) {
                               handled = code == 0 && payload == "payload";
                               handler_thread = std::this_thread::get_id();
                           }));
    for (int attempt = 0; attempt < 100 && !handled; ++attempt) {
        adapter.drain([](SoundCardApiAdapter::Result &result) {
            if (result.handler) result.handler(result.code, std::move(result.data));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(handled);
    assert(handler_thread == std::this_thread::get_id());
    std::lock_guard<std::mutex> lock(calls_mutex);
    assert(calls.size() == 1);
    assert(calls[0].size() == 1 && calls[0][0] == "ListCards");
}

void test_adapter_shutdown_joins_backend_task()
{
    bool finished = false;
    {
        SoundCardApiAdapter adapter(
            [&](std::list<std::string>, SoundCardApiAdapter::Completion callback) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                finished = true;
                callback(-1, "failed");
            });
        assert(adapter.request({"ListCards"}, 1, [](int, std::string) {}));
    }
    assert(finished);
}

void test_adapter_reports_missing_callback()
{
    SoundCardApiAdapter adapter(
        [](std::list<std::string>, SoundCardApiAdapter::Completion) {},
        std::chrono::milliseconds(1));
    bool handled = false;
    int result_code = 0;
    std::string result_data;
    assert(adapter.request({"ListCards"}, 2,
                           [&](int code, std::string data) {
                               handled = true;
                               result_code = code;
                               result_data = std::move(data);
                           }));
    for (int attempt = 0; attempt < 100 && !handled; ++attempt) {
        adapter.drain([](SoundCardApiAdapter::Result &result) {
            if (result.handler) result.handler(result.code, std::move(result.data));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(handled);
    assert(result_code != 0);
    assert(result_data == "Soundcard request timed out");
}

void test_adapter_discards_pending_result()
{
    SoundCardApiAdapter adapter(
        [](std::list<std::string>, SoundCardApiAdapter::Completion callback) {
            callback(0, "stale");
        });
    bool handled = false;
    assert(adapter.request({"ListCards"}, 3,
                           [&](int, std::string) { handled = true; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    adapter.discard_pending();
    adapter.drain([](SoundCardApiAdapter::Result &result) {
        if (result.handler) result.handler(result.code, std::move(result.data));
    });
    assert(!handled);
}

void test_adapter_reports_missing_invoker()
{
    SoundCardApiAdapter adapter(SoundCardApiAdapter::Invoker{});
    bool handled = false;
    int result_code = 0;
    assert(!adapter.request({"ListCards"}, 4,
                            [&](int code, std::string) {
                                handled = true;
                                result_code = code;
                            }));
    adapter.drain([](SoundCardApiAdapter::Result &result) {
        if (result.handler) result.handler(result.code, std::move(result.data));
    });
    assert(handled);
    assert(result_code != 0);
}

}

int main()
{
    test_card_and_control_payloads();
    test_detail_and_range_rules();
    test_malformed_control_rows_are_ignored();
    test_detail_payload_validation_accepts_all_channels();
    test_adapter_queues_results_on_drain_thread();
    test_adapter_shutdown_joins_backend_task();
    test_adapter_reports_missing_callback();
    test_adapter_discards_pending_result();
    test_adapter_reports_missing_invoker();
    return 0;
}
