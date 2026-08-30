#pragma once

#include <list>
#include <functional>
#include <string>
#include <vector>

namespace cp0::audio {

enum class ApiCommand {
    PlayFile,
    Play,
    PlayPause,
    PlayContinue,
    PlayEnd,
    Cap,
    CapPause,
    CapContinue,
    CapEnd,
    CapFileSave,
    SetCallback,
    VolumeRead,
    VolumeWrite,
    MuteRead,
    MuteToggle,
    SetSystemSoundNames,
    SystemSoundPlay,
    SystemSoundSuspend,
    SystemSoundPrepare,
    SystemSoundEnable,
};

struct ApiRequest
{
    ApiCommand command = ApiCommand::Play;
    std::string path;
    int value = 0;
    bool enabled = false;
    std::vector<std::string> names;
};

enum class SetupCommand { SetCallback, SetWaveform, StopPlay };

struct SetupRequest
{
    SetupCommand command = SetupCommand::SetCallback;
    bool waveform_enabled = false;
};

bool parse_api_request(const std::list<std::string> &args, ApiRequest &request);
bool parse_setup_request(const std::list<std::string> &args, SetupRequest &request);
const char *invalid_api_request_message();
const char *invalid_setup_request_message();
void invoke_callback(const std::function<void(int, std::string)> &callback,
                     int code, const std::string &data) noexcept;

} // namespace cp0::audio
