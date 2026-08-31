#include "cp0_audio_api_contract.hpp"

#include <cassert>
#include <stdexcept>
#include <string>

namespace {

using cp0::audio::ApiCommand;
using cp0::audio::ApiRequest;
using cp0::audio::SetupCommand;
using cp0::audio::SetupRequest;

void test_path_and_no_argument_commands()
{
    ApiRequest request;
    assert(!cp0::audio::parse_api_request({}, request));
    assert(!cp0::audio::parse_api_request({"Unknown"}, request));
    assert(!cp0::audio::parse_api_request({"Play"}, request));
    assert(!cp0::audio::parse_api_request({"Play", ""}, request));
    assert(!cp0::audio::parse_api_request({"Play", "tone.wav", "junk"}, request));
    assert(cp0::audio::parse_api_request({"Play", "tone.wav"}, request));
    assert(request.command == ApiCommand::Play && request.path == "tone.wav");
    assert(cp0::audio::parse_api_request({"PlayFile", "/tmp/tone.wav"}, request));
    assert(request.command == ApiCommand::PlayFile && request.path == "/tmp/tone.wav");
    assert(cp0::audio::parse_api_request({"CapFileSave", "/tmp/cap.wav"}, request));
    assert(request.command == ApiCommand::CapFileSave);

    assert(cp0::audio::parse_api_request({"PlayPause"}, request));
    assert(request.command == ApiCommand::PlayPause);
    assert(!cp0::audio::parse_api_request({"PlayPause", "junk"}, request));
    assert(cp0::audio::parse_api_request({"CapEnd"}, request));
    assert(request.command == ApiCommand::CapEnd);
    assert(cp0::audio::parse_api_request({"MuteToggle"}, request));
    assert(request.command == ApiCommand::MuteToggle);
    assert(cp0::audio::parse_api_request({"SystemSoundSuspend"}, request));
    assert(request.command == ApiCommand::SystemSoundSuspend);
    assert(cp0::audio::parse_api_request({"SystemSoundPrepare"}, request));
    assert(request.command == ApiCommand::SystemSoundPrepare);
    assert(!cp0::audio::parse_api_request({"SystemSoundPrepare", "junk"}, request));
}

void test_numeric_and_boolean_commands()
{
    ApiRequest request;
    assert(cp0::audio::parse_api_request({"VolumeWrite", "0"}, request));
    assert(request.command == ApiCommand::VolumeWrite && request.value == 0);
    assert(cp0::audio::parse_api_request({"VolumeWrite", "100"}, request));
    assert(request.value == 100);
    assert(!cp0::audio::parse_api_request({"VolumeWrite"}, request));
    assert(!cp0::audio::parse_api_request({"VolumeWrite", ""}, request));
    assert(!cp0::audio::parse_api_request({"VolumeWrite", "12junk"}, request));
    assert(!cp0::audio::parse_api_request({"VolumeWrite", "-1"}, request));
    assert(!cp0::audio::parse_api_request({"VolumeWrite", "101"}, request));
    assert(!cp0::audio::parse_api_request({"VolumeWrite", "99999999999999999999"}, request));
    assert(!cp0::audio::parse_api_request({"VolumeWrite", "50", "junk"}, request));

    assert(cp0::audio::parse_api_request({"SystemSoundPlay", "0"}, request));
    assert(request.command == ApiCommand::SystemSoundPlay && request.value == 0);
    assert(cp0::audio::parse_api_request({"SystemSoundPlay", "2"}, request));
    assert(!cp0::audio::parse_api_request({"SystemSoundPlay", "3"}, request));
    assert(!cp0::audio::parse_api_request({"SystemSoundPlay", "junk"}, request));

    assert(cp0::audio::parse_api_request({"SystemSoundEnable"}, request));
    assert(request.enabled);
    assert(cp0::audio::parse_api_request({"SystemSoundEnable", "disabled"}, request));
    assert(!request.enabled);
    assert(cp0::audio::parse_api_request({"SystemSoundEnable", "true"}, request));
    assert(request.enabled);
    assert(!cp0::audio::parse_api_request({"SystemSoundEnable", "yes"}, request));
}

void test_names_and_setup_commands()
{
    ApiRequest request;
    assert(cp0::audio::parse_api_request({"SetSystemSoundNames"}, request));
    assert(request.names.empty());
    assert(cp0::audio::parse_api_request(
        {"SetSystemSoundNames", "ding.wav", "switch.wav", "enter.wav"}, request));
    assert(request.names.size() == 3 && request.names[2] == "enter.wav");
    assert(!cp0::audio::parse_api_request(
        {"SetSystemSoundNames", "1", "2", "3", "4"}, request));

    SetupRequest setup;
    assert(!cp0::audio::parse_setup_request({}, setup));
    assert(!cp0::audio::parse_setup_request({"unknown"}, setup));
    assert(cp0::audio::parse_setup_request({"set_callback"}, setup));
    assert(setup.command == SetupCommand::SetCallback);
    assert(!cp0::audio::parse_setup_request({"set_callback", "junk"}, setup));
    assert(cp0::audio::parse_setup_request({"waveform"}, setup));
    assert(setup.command == SetupCommand::SetWaveform && setup.waveform_enabled);
    assert(cp0::audio::parse_setup_request({"set_waveform", "off"}, setup));
    assert(!setup.waveform_enabled);
    assert(!cp0::audio::parse_setup_request({"waveform", "junk"}, setup));
    assert(!cp0::audio::parse_setup_request({"waveform", "on", "junk"}, setup));
    assert(cp0::audio::parse_setup_request({"stop_play"}, setup));
    assert(setup.command == SetupCommand::StopPlay);
    assert(std::string(cp0::audio::invalid_api_request_message()) ==
        "invalid audio api request\n");
}

void test_callback_exception_boundary()
{
    int calls = 0;
    cp0::audio::invoke_callback(
        [&](int code, std::string data) {
            ++calls;
            assert(code == 7 && data == "result");
            throw std::runtime_error("callback");
        },
        7, "result");
    assert(calls == 1);
    cp0::audio::invoke_callback({}, 0, "ignored");
}

} // namespace

int main()
{
    test_path_and_no_argument_commands();
    test_numeric_and_boolean_commands();
    test_names_and_setup_commands();
    test_callback_exception_boundary();
}
