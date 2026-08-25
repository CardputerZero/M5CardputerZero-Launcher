#pragma once

#include <chrono>
#include <functional>
#include <list>
#include <string>
#include <string_view>

namespace settings_audio {

constexpr int kMinVolume = 0;
constexpr int kMaxVolume = 100;
constexpr int kSystemSoundPreviewIndex = 2;

constexpr int kErrorInvalid = -1;
constexpr int kErrorTimeout = -2;
constexpr int kErrorInvoker = -3;

struct Response {
    int code = kErrorInvoker;
    std::string data;
};

struct VolumeResponse {
    int code = kErrorInvoker;
    int value = -1;
    std::string data;
};

struct EnableResponse {
    int code = kErrorInvoker;
    bool enabled = false;
    std::string data;
};

using ApiCallback = std::function<void(int, std::string)>;
using ApiInvoker = std::function<void(std::list<std::string>, ApiCallback)>;

bool volume_value_valid(int value);
int round_volume_percent(int value);
int volume_index(int value);
int volume_percent(int index);

bool parse_volume_payload(int code, std::string_view data, int &value);
bool parse_enabled_payload(int code, std::string_view data, bool &enabled);

Response invoke(const std::list<std::string> &args,
                const ApiInvoker &invoker,
                std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
Response invoke(const std::list<std::string> &args,
                std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

VolumeResponse read_volume();
VolumeResponse read_volume(const ApiInvoker &invoker,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
VolumeResponse write_volume(int value);
VolumeResponse write_volume(int value,
                            const ApiInvoker &invoker,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
Response play_system_sound(int index = kSystemSoundPreviewIndex);
Response play_system_sound(int index,
                           const ApiInvoker &invoker,
                           std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
EnableResponse set_system_sound_enabled(bool enabled);
EnableResponse set_system_sound_enabled(bool enabled,
                                         const ApiInvoker &invoker,
                                         std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

} // namespace settings_audio
