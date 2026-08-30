#include "cp0_audio_api_contract.hpp"

#include <charconv>
#include <iterator>
#include <string_view>

namespace cp0::audio {
namespace {

bool parse_integer(std::string_view text, int minimum, int maximum, int &value)
{
    if (text.empty())
        return false;
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed < minimum || parsed > maximum)
        return false;
    value = parsed;
    return true;
}

bool parse_boolean(std::string_view text, bool &enabled)
{
    if (text == "1" || text == "on" || text == "true" || text == "enable" || text == "enabled") {
        enabled = true;
        return true;
    }
    if (text == "0" || text == "off" || text == "false" || text == "disable" || text == "disabled") {
        enabled = false;
        return true;
    }
    return false;
}

bool is_no_argument_command(const std::string &command, ApiCommand &parsed)
{
    if (command == "PlayPause") parsed = ApiCommand::PlayPause;
    else if (command == "PlayContinue") parsed = ApiCommand::PlayContinue;
    else if (command == "PlayEnd") parsed = ApiCommand::PlayEnd;
    else if (command == "Cap") parsed = ApiCommand::Cap;
    else if (command == "CapPause") parsed = ApiCommand::CapPause;
    else if (command == "CapContinue") parsed = ApiCommand::CapContinue;
    else if (command == "CapEnd") parsed = ApiCommand::CapEnd;
    else if (command == "SetCallback") parsed = ApiCommand::SetCallback;
    else if (command == "VolumeRead") parsed = ApiCommand::VolumeRead;
    else if (command == "MuteRead") parsed = ApiCommand::MuteRead;
    else if (command == "MuteToggle") parsed = ApiCommand::MuteToggle;
    else if (command == "SystemSoundSuspend") parsed = ApiCommand::SystemSoundSuspend;
    else if (command == "SystemSoundPrepare") parsed = ApiCommand::SystemSoundPrepare;
    else return false;
    return true;
}

} // namespace

bool parse_api_request(const std::list<std::string> &args, ApiRequest &request)
{
    request = {};
    if (args.empty())
        return false;
    auto argument = args.begin();
    const std::string &command = *argument++;

    if (is_no_argument_command(command, request.command))
        return args.size() == 1;
    if (command == "PlayFile" || command == "Play" || command == "CapFileSave") {
        request.command = command == "PlayFile" ? ApiCommand::PlayFile :
            (command == "Play" ? ApiCommand::Play : ApiCommand::CapFileSave);
        if (args.size() != 2 || argument->empty())
            return false;
        request.path = *argument;
        return true;
    }
    if (command == "VolumeWrite") {
        request.command = ApiCommand::VolumeWrite;
        return args.size() == 2 && parse_integer(*argument, 0, 100, request.value);
    }
    if (command == "SystemSoundPlay") {
        request.command = ApiCommand::SystemSoundPlay;
        return args.size() == 2 && parse_integer(*argument, 0, 2, request.value);
    }
    if (command == "SystemSoundEnable") {
        request.command = ApiCommand::SystemSoundEnable;
        if (args.size() == 1) {
            request.enabled = true;
            return true;
        }
        return args.size() == 2 && parse_boolean(*argument, request.enabled);
    }
    if (command == "SetSystemSoundNames") {
        request.command = ApiCommand::SetSystemSoundNames;
        if (args.size() > 4)
            return false;
        request.names.assign(argument, args.end());
        return true;
    }
    return false;
}

bool parse_setup_request(const std::list<std::string> &args, SetupRequest &request)
{
    request = {};
    if (args.empty())
        return false;
    const std::string &command = args.front();
    if (command == "set_callback") {
        request.command = SetupCommand::SetCallback;
        return args.size() == 1;
    }
    if (command == "stop_play") {
        request.command = SetupCommand::StopPlay;
        return args.size() == 1;
    }
    if (command == "set_waveform" || command == "waveform") {
        request.command = SetupCommand::SetWaveform;
        if (args.size() == 1) {
            request.waveform_enabled = true;
            return true;
        }
        auto value = std::next(args.begin());
        return args.size() == 2 && parse_boolean(*value, request.waveform_enabled);
    }
    return false;
}

const char *invalid_api_request_message()
{
    return "invalid audio api request\n";
}

const char *invalid_setup_request_message()
{
    return "invalid audio setup request\n";
}

void invoke_callback(const std::function<void(int, std::string)> &callback,
                     int code, const std::string &data) noexcept
{
    if (!callback) return;
    try {
        callback(code, data);
    } catch (...) {
    }
}

} // namespace cp0::audio
