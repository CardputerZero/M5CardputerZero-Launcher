#include "../main/ui/launcher_media_controls.h"

#include "hal_lvgl_bsp.h"

#include <cassert>
#include <functional>
#include <iterator>
#include <list>
#include <string>
#include <vector>

eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_audio_api;
eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_config_api;
eventpp::CallbackList<void(std::list<std::string>, std::function<void(int, std::string)>)>
    cp0_signal_settings_api;

namespace {

int sink_volume = 80;
int configured_volume = 80;
bool sink_muted = true;
int mute_toggles = 0;
std::vector<std::string> audio_commands;

void reply(const std::function<void(int, std::string)> &callback,
           int code, const std::string &data)
{
    if (callback)
        callback(code, data);
}

} // namespace

int main()
{
    cp0_signal_audio_api.append(
        [](std::list<std::string> arguments,
           std::function<void(int, std::string)> callback) {
            assert(!arguments.empty());
            const std::string command = arguments.front();
            audio_commands.push_back(command);

            if (command == "MuteRead") {
                reply(callback, 0, sink_muted ? "1" : "0");
            } else if (command == "MuteToggle") {
                sink_muted = !sink_muted;
                ++mute_toggles;
                reply(callback, 0, sink_muted ? "1" : "0");
            } else if (command == "VolumeRead") {
                reply(callback, 0, std::to_string(sink_volume));
            } else if (command == "VolumeWrite") {
                assert(arguments.size() == 2);
                sink_volume = std::stoi(*std::next(arguments.begin()));
                reply(callback, 0, std::to_string(sink_volume));
            } else {
                assert(false);
            }
        });

    cp0_signal_config_api.append(
        [](std::list<std::string> arguments,
           std::function<void(int, std::string)> callback) {
            assert(!arguments.empty());
            const std::string command = arguments.front();
            if (command == "GetInt") {
                reply(callback, 0, std::to_string(configured_volume));
            } else if (command == "SetInt") {
                assert(arguments.size() == 3);
                configured_volume = std::stoi(*std::next(arguments.begin(), 2));
                reply(callback, 0, "");
            } else if (command == "Save") {
                reply(callback, 0, "");
            } else {
                assert(false);
            }
        });

    assert(launcher_media_controls::adjust_volume(-5) == 75);
    assert(!sink_muted);
    assert(sink_volume == 75);
    assert(configured_volume == 75);
    assert(mute_toggles == 1);
    assert(audio_commands.size() >= 4);
    assert(audio_commands[0] == "MuteRead");
    assert(audio_commands[1] == "MuteToggle");
    assert(audio_commands[2] == "VolumeRead");
    assert(audio_commands[3] == "VolumeWrite");

    sink_muted = true;
    assert(launcher_media_controls::adjust_volume(5) == 80);
    assert(!sink_muted);
    assert(sink_volume == 80);
    assert(configured_volume == 80);
    assert(mute_toggles == 2);

    assert(launcher_media_controls::adjust_volume(-5) == 75);
    assert(!sink_muted);
    assert(sink_volume == 75);
    assert(configured_volume == 75);
    assert(mute_toggles == 2);
    return 0;
}
