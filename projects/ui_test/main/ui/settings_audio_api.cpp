#include "settings_audio_api.hpp"

#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>

namespace settings_audio {
namespace {

struct WaitState {
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    Response response;
};

bool parse_integer(std::string_view data, int minimum, int maximum, int &value)
{
    if (data.empty()) return false;

    int parsed = 0;
    const auto result = std::from_chars(data.data(), data.data() + data.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != data.data() + data.size() ||
        parsed < minimum || parsed > maximum)
        return false;

    value = parsed;
    return true;
}

bool parse_boolean(std::string_view data, bool &enabled)
{
    if (data == "1" || data == "on" || data == "true" || data == "enable" || data == "enabled") {
        enabled = true;
        return true;
    }
    if (data == "0" || data == "off" || data == "false" || data == "disable" || data == "disabled") {
        enabled = false;
        return true;
    }
    return false;
}

Response default_invoke(const std::list<std::string> &args, std::chrono::milliseconds timeout)
{
    return invoke(
        args,
        [](std::list<std::string> command, ApiCallback callback) {
            cp0_signal_audio_api(std::move(command), std::move(callback));
        },
        timeout);
}

VolumeResponse decode_volume_response(const Response &response)
{
    VolumeResponse result{response.code, -1, response.data};
    if (!parse_volume_payload(response.code, response.data, result.value)) {
        if (response.code == 0) result.code = kErrorInvalid;
        result.value = -1;
    }
    return result;
}

EnableResponse decode_enable_response(const Response &response)
{
    EnableResponse result{response.code, false, response.data};
    if (!parse_enabled_payload(response.code, response.data, result.enabled) && response.code == 0)
        result.code = kErrorInvalid;
    return result;
}

} // namespace

bool volume_value_valid(int value)
{
    return value >= kMinVolume && value <= kMaxVolume;
}

int round_volume_percent(int value)
{
    const int clamped = std::clamp(value, kMinVolume, kMaxVolume);
    return std::min(kMaxVolume, ((clamped + 5) / 10) * 10);
}

int volume_index(int value)
{
    return (kMaxVolume - round_volume_percent(value)) / 10;
}

int volume_percent(int index)
{
    const int clamped_index = std::clamp(index, 0, 10);
    return kMaxVolume - clamped_index * 10;
}

bool parse_volume_payload(int code, std::string_view data, int &value)
{
    if (code != 0) return false;
    return parse_integer(data, kMinVolume, kMaxVolume, value);
}

bool parse_enabled_payload(int code, std::string_view data, bool &enabled)
{
    if (code != 0) return false;
    return parse_boolean(data, enabled);
}

Response invoke(const std::list<std::string> &args,
                const ApiInvoker &invoker,
                std::chrono::milliseconds timeout)
{
    if (!invoker) return {kErrorInvoker, "audio api invoker unavailable"};

    auto state = std::make_shared<WaitState>();
    try {
        invoker(args, [state](int code, std::string data) noexcept {
            try {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->completed) return;
                state->response.code = code;
                state->response.data = std::move(data);
                state->completed = true;
                state->condition.notify_one();
            } catch (...) {
            }
        });
    } catch (...) {
        return {kErrorInvoker, "audio api invocation failed"};
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->condition.wait_for(lock, timeout, [state] { return state->completed; }))
        return {kErrorTimeout, "audio api timeout"};
    return state->response;
}

Response invoke(const std::list<std::string> &args, std::chrono::milliseconds timeout)
{
    return default_invoke(args, timeout);
}

VolumeResponse read_volume()
{
    return decode_volume_response(invoke({"VolumeRead"}));
}

VolumeResponse read_volume(const ApiInvoker &invoker, std::chrono::milliseconds timeout)
{
    return decode_volume_response(invoke({"VolumeRead"}, invoker, timeout));
}

VolumeResponse write_volume(int value)
{
    if (!volume_value_valid(value)) return {kErrorInvalid, -1, "volume out of range"};

    return decode_volume_response(invoke({"VolumeWrite", std::to_string(value)}));
}

VolumeResponse write_volume(int value, const ApiInvoker &invoker, std::chrono::milliseconds timeout)
{
    if (!volume_value_valid(value)) return {kErrorInvalid, -1, "volume out of range"};
    return decode_volume_response(invoke({"VolumeWrite", std::to_string(value)}, invoker, timeout));
}

Response play_system_sound(int index)
{
    if (index < 0 || index > 2) return {kErrorInvalid, "system sound index out of range"};
    return invoke({"SystemSoundPlay", std::to_string(index)});
}

Response play_system_sound(int index, const ApiInvoker &invoker, std::chrono::milliseconds timeout)
{
    if (index < 0 || index > 2) return {kErrorInvalid, "system sound index out of range"};
    return invoke({"SystemSoundPlay", std::to_string(index)}, invoker, timeout);
}

EnableResponse set_system_sound_enabled(bool enabled)
{
    return decode_enable_response(invoke({"SystemSoundEnable", enabled ? "1" : "0"}));
}

EnableResponse set_system_sound_enabled(bool enabled,
                                         const ApiInvoker &invoker,
                                         std::chrono::milliseconds timeout)
{
    return decode_enable_response(
        invoke({"SystemSoundEnable", enabled ? "1" : "0"}, invoker, timeout));
}

} // namespace settings_audio
