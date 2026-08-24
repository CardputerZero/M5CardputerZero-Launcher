#include "settings_audio_api.hpp"

#include "hal_lvgl_bsp.h"

#include <cassert>
#include <chrono>
#include <list>
#include <stdexcept>
#include <string>
#include <thread>

eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_audio_api;

namespace {

struct FakeResponse {
    std::list<std::string> arguments;
    int code = 0;
    std::string data;
    bool invoke = true;
    bool invoke_twice = false;
    bool throw_invoker = false;
    bool callback_on_worker = false;
};

settings_audio::ApiInvoker make_invoker(FakeResponse &response)
{
    return [&response](std::list<std::string> arguments, settings_audio::ApiCallback callback) {
        response.arguments = std::move(arguments);
        if (response.throw_invoker) throw std::runtime_error("invoker failure");
        if (!response.invoke || !callback) return;

        auto deliver = [&response, callback = std::move(callback)]() mutable {
            callback(response.code, response.data);
            if (response.invoke_twice) callback(0, "99");
        };
        if (response.callback_on_worker) {
            std::thread worker(std::move(deliver));
            worker.join();
        } else {
            deliver();
        }
    };
}

void test_volume_mapping()
{
    assert(!settings_audio::volume_value_valid(-1));
    assert(settings_audio::volume_value_valid(0));
    assert(settings_audio::volume_value_valid(50));
    assert(settings_audio::volume_value_valid(100));
    assert(!settings_audio::volume_value_valid(101));

    assert(settings_audio::round_volume_percent(-5) == 0);
    assert(settings_audio::round_volume_percent(0) == 0);
    assert(settings_audio::round_volume_percent(4) == 0);
    assert(settings_audio::round_volume_percent(5) == 10);
    assert(settings_audio::round_volume_percent(49) == 50);
    assert(settings_audio::round_volume_percent(50) == 50);
    assert(settings_audio::round_volume_percent(95) == 100);
    assert(settings_audio::round_volume_percent(101) == 100);

    assert(settings_audio::volume_index(0) == 10);
    assert(settings_audio::volume_index(50) == 5);
    assert(settings_audio::volume_index(100) == 0);
    assert(settings_audio::volume_index(55) == 4);
    assert(settings_audio::volume_percent(0) == 100);
    assert(settings_audio::volume_percent(5) == 50);
    assert(settings_audio::volume_percent(10) == 0);
    assert(settings_audio::volume_percent(-1) == 100);
    assert(settings_audio::volume_percent(11) == 0);
}

void test_volume_read_and_payload_validation()
{
    FakeResponse response;
    response.data = "50";
    response.callback_on_worker = true;
    response.invoke_twice = true;
    const auto result = settings_audio::read_volume(make_invoker(response),
                                                    std::chrono::milliseconds(100));
    assert(result.code == 0);
    assert(result.value == 50);
    assert(response.arguments == std::list<std::string>{"VolumeRead"});

    response.data = "50\n";
    const auto malformed = settings_audio::read_volume(make_invoker(response),
                                                      std::chrono::milliseconds(100));
    assert(malformed.code == settings_audio::kErrorInvalid);
    assert(malformed.value == -1);

    response.code = -7;
    response.data = "backend failed";
    const auto failed = settings_audio::read_volume(make_invoker(response),
                                                    std::chrono::milliseconds(100));
    assert(failed.code == -7);
    assert(failed.value == -1);
}

void test_volume_write_validation_and_actual_value()
{
    FakeResponse response;
    response.data = "40";
    const auto invoker = make_invoker(response);

    const auto applied = settings_audio::write_volume(50, invoker,
                                                      std::chrono::milliseconds(100));
    assert(applied.code == 0);
    assert(applied.value == 40);
    const std::list<std::string> write_arguments{"VolumeWrite", "50"};
    assert(response.arguments == write_arguments);

    response.arguments.clear();
    const auto low = settings_audio::write_volume(-1, invoker,
                                                  std::chrono::milliseconds(100));
    assert(low.code == settings_audio::kErrorInvalid);
    assert(low.value == -1);
    assert(response.arguments.empty());

    const auto high = settings_audio::write_volume(101, invoker,
                                                   std::chrono::milliseconds(100));
    assert(high.code == settings_audio::kErrorInvalid);
    assert(high.value == -1);
    assert(response.arguments.empty());

    response.code = 0;
    response.data = "101";
    const auto malformed = settings_audio::write_volume(50, invoker,
                                                       std::chrono::milliseconds(100));
    assert(malformed.code == settings_audio::kErrorInvalid);
    assert(malformed.value == -1);
}

void test_system_sound_commands()
{
    FakeResponse response;
    response.data = "system sound play";
    const auto invoker = make_invoker(response);

    const auto played = settings_audio::play_system_sound(2, invoker,
                                                          std::chrono::milliseconds(100));
    assert(played.code == 0);
    const std::list<std::string> play_arguments{"SystemSoundPlay", "2"};
    assert(response.arguments == play_arguments);

    response.arguments.clear();
    const auto invalid_low = settings_audio::play_system_sound(-1, invoker,
                                                                std::chrono::milliseconds(100));
    assert(invalid_low.code == settings_audio::kErrorInvalid);
    assert(response.arguments.empty());

    const auto invalid_high = settings_audio::play_system_sound(3, invoker,
                                                                 std::chrono::milliseconds(100));
    assert(invalid_high.code == settings_audio::kErrorInvalid);
    assert(response.arguments.empty());

    response.data = "1";
    const auto enabled = settings_audio::set_system_sound_enabled(true, invoker,
                                                                   std::chrono::milliseconds(100));
    assert(enabled.code == 0);
    assert(enabled.enabled);
    const std::list<std::string> enable_arguments{"SystemSoundEnable", "1"};
    assert(response.arguments == enable_arguments);

    response.data = "0";
    const auto disabled = settings_audio::set_system_sound_enabled(false, invoker,
                                                                    std::chrono::milliseconds(100));
    assert(disabled.code == 0);
    assert(!disabled.enabled);
    const std::list<std::string> disable_arguments{"SystemSoundEnable", "0"};
    assert(response.arguments == disable_arguments);

    response.data = "maybe";
    const auto malformed = settings_audio::set_system_sound_enabled(true, invoker,
                                                                    std::chrono::milliseconds(100));
    assert(malformed.code == settings_audio::kErrorInvalid);
    assert(!malformed.enabled);
}

void test_invocation_failures()
{
    FakeResponse no_callback;
    no_callback.invoke = false;
    const auto timeout = settings_audio::read_volume(make_invoker(no_callback),
                                                     std::chrono::milliseconds(1));
    assert(timeout.code == settings_audio::kErrorTimeout);

    FakeResponse throwing;
    throwing.throw_invoker = true;
    const auto failed = settings_audio::read_volume(make_invoker(throwing),
                                                    std::chrono::milliseconds(100));
    assert(failed.code == settings_audio::kErrorInvoker);

    const auto unavailable = settings_audio::invoke(
        {"VolumeRead"}, settings_audio::ApiInvoker{}, std::chrono::milliseconds(100));
    assert(unavailable.code == settings_audio::kErrorInvoker);
}

}

int main()
{
    test_volume_mapping();
    test_volume_read_and_payload_validation();
    test_volume_write_validation_and_actual_value();
    test_system_sound_commands();
    test_invocation_failures();
    return 0;
}
