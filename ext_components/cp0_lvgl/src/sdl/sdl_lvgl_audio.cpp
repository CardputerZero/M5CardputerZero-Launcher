#include "hal_lvgl_bsp.h"
#include "../cp0_audio_api_contract.hpp"
#include "../cp0_audio_runtime_lifecycle.hpp"
#include "../cp0_signal_registration.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace {

class AudioSystem
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = std::list<std::string>;

    int play(const std::string &wav)
    {
        (void)wav;
        playing_ = true;
        return 0;
    }

    void cap(bool enable)
    {
        capturing_ = enable;
        if (!enable)
            cap_paused_ = false;
    }

    void setup(arg_t arg, callback_t callback)
    {
        cp0::audio::SetupRequest request;
        if (!cp0::audio::parse_setup_request(arg, request)) {
            report(callback, -1, cp0::audio::invalid_setup_request_message());
            return;
        }
        if (request.command == cp0::audio::SetupCommand::SetCallback) {
            status_callback_ = std::move(callback);
        } else if (request.command == cp0::audio::SetupCommand::SetWaveform) {
            waveform_enabled_ = request.waveform_enabled;
            emit_waveform_once();
        } else {
            playing_ = false;
        }
    }

    void system_play(const std::string &name)
    {
        if (!system_sound_enabled_)
            return;
        play(name);
    }

    void api_call(arg_t arg, callback_t callback)
    {
        cp0::audio::ApiRequest request;
        if (!cp0::audio::parse_api_request(arg, request)) {
            report(callback, -1, cp0::audio::invalid_api_request_message());
            return;
        }

#define map_fun(name) {#name, std::bind(&AudioSystem::name, this, std::placeholders::_1, std::placeholders::_2)}
        std::list<std::pair<std::string, std::function<void(arg_t, callback_t)>>> cmd_map = {
            map_fun(PlayFile),
            map_fun(Play),
            map_fun(PlayPause),
            map_fun(PlayContinue),
            map_fun(PlayEnd),
            map_fun(Cap),
            map_fun(CapPause),
            map_fun(CapContinue),
            map_fun(CapEnd),
            map_fun(CapFileSave),
            map_fun(SetCallback),
            map_fun(VolumeRead),
            map_fun(MuteRead),
            map_fun(MuteToggle),
            map_fun(SetSystemSoundNames),
        };
#undef map_fun

        if (request.command == cp0::audio::ApiCommand::VolumeWrite) {
            VolumeWrite(request.value, callback);
            return;
        }
        if (request.command == cp0::audio::ApiCommand::SystemSoundPlay) {
            SystemSoundPlay(request.value, callback);
            return;
        }
        if (request.command == cp0::audio::ApiCommand::SystemSoundSuspend) {
            SystemSoundSuspend(arg, callback);
            return;
        }
        if (request.command == cp0::audio::ApiCommand::SystemSoundPrepare) {
            SystemSoundPrepare(arg, callback);
            return;
        }
        if (request.command == cp0::audio::ApiCommand::SystemSoundEnable) {
            SystemSoundEnable(request.enabled, callback);
            return;
        }

        for (const auto &it : cmd_map) {
            if (it.first == arg.front()) {
                it.second(arg, callback);
                return;
            }
        }
        report(callback, -1, "unknown audio api\n");
    }

private:
    static constexpr int kDefaultVolume = 39;
    static constexpr int kMaxVolume = 100;
    static constexpr int kRecWaveformSize = 128;

    callback_t status_callback_;
    bool playing_ = false;
    bool play_paused_ = false;
    bool capturing_ = false;
    bool cap_paused_ = false;
    bool waveform_enabled_ = false;
    int volume_ = kDefaultVolume;
    bool muted_ = false;
    std::array<std::string, 3> system_sound_names_ = {"Ding2.wav", "switch.wav", "enter.wav"};
    bool system_sound_enabled_ = true;

    void report(callback_t callback, int code, const std::string &data)
    {
        cp0::audio::invoke_callback(callback ? callback : status_callback_, code, data);
    }

    static std::string first_arg_after_command(const arg_t &arg)
    {
        if (arg.size() < 2)
            return "";
        return *std::next(arg.begin());
    }

    void emit_waveform_once()
    {
        if (!waveform_enabled_ || !status_callback_)
            return;
        std::string waveform(sizeof(float) * kRecWaveformSize, '\0');
        cp0::audio::invoke_callback(status_callback_, 1, waveform);
    }

    static void put_u16_le(std::uint8_t *p, std::uint16_t v)
    {
        p[0] = static_cast<std::uint8_t>(v & 0xFF);
        p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    }

    static void put_u32_le(std::uint8_t *p, std::uint32_t v)
    {
        p[0] = static_cast<std::uint8_t>(v & 0xFF);
        p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    }

    static int write_silent_wav(const std::string &path)
    {
        if (path.empty())
            return -1;

        std::uint8_t header[44] = {};
        std::memcpy(header + 0, "RIFF", 4);
        put_u32_le(header + 4, 36);
        std::memcpy(header + 8, "WAVEfmt ", 8);
        put_u32_le(header + 16, 16);
        put_u16_le(header + 20, 1);
        put_u16_le(header + 22, 2);
        put_u32_le(header + 24, 48000);
        put_u32_le(header + 28, 48000 * 2 * 2);
        put_u16_le(header + 32, 4);
        put_u16_le(header + 34, 16);
        std::memcpy(header + 36, "data", 4);
        put_u32_le(header + 40, 0);

        FILE *fp = std::fopen(path.c_str(), "wb");
        if (!fp)
            return -2;
        size_t written = std::fwrite(header, 1, sizeof(header), fp);
        int close_ret = std::fclose(fp);
        return (written == sizeof(header) && close_ret == 0) ? 0 : -3;
    }

    void PlayFile(arg_t arg, callback_t callback)
    {
        std::string file = first_arg_after_command(arg);
        if (file.empty()) {
            report(callback, -1, "PlayFile need file\n");
            return;
        }
        int ret = play(file);
        report(callback, ret, ret == 0 ? "play start\n" : "play failed\n");
    }

    void Play(arg_t arg, callback_t callback)
    {
        std::string file = first_arg_after_command(arg);
        if (file.empty()) {
            report(callback, -1, "Play need file\n");
            return;
        }
        int ret = play(file);
        report(callback, ret, ret == 0 ? "play start\n" : "play failed\n");
    }

    void PlayPause(arg_t arg, callback_t callback)
    {
        (void)arg;
        if (!playing_) {
            report(callback, -1, "play not started\n");
            return;
        }
        play_paused_ = true;
        report(callback, 0, "play pause\n");
    }

    void PlayContinue(arg_t arg, callback_t callback)
    {
        (void)arg;
        if (!playing_) {
            report(callback, -1, "play not started\n");
            return;
        }
        play_paused_ = false;
        report(callback, 0, "play continue\n");
    }

    void PlayEnd(arg_t arg, callback_t callback)
    {
        (void)arg;
        playing_ = false;
        play_paused_ = false;
        report(callback, 0, "play stop\n");
    }

    void Cap(arg_t arg, callback_t callback)
    {
        (void)arg;
        capturing_ = true;
        cap_paused_ = false;
        emit_waveform_once();
        report(callback, 0, "cap start\n");
    }

    void CapPause(arg_t arg, callback_t callback)
    {
        (void)arg;
        if (!capturing_) {
            report(callback, -1, "cap not started\n");
            return;
        }
        cap_paused_ = true;
        report(callback, 0, "cap pause\n");
    }

    void CapContinue(arg_t arg, callback_t callback)
    {
        (void)arg;
        if (!capturing_) {
            report(callback, -1, "cap not started\n");
            return;
        }
        cap_paused_ = false;
        emit_waveform_once();
        report(callback, 0, "cap continue\n");
    }

    void CapEnd(arg_t arg, callback_t callback)
    {
        (void)arg;
        capturing_ = false;
        cap_paused_ = false;
        report(callback, 0, "cap stop\n");
    }

    void CapFileSave(arg_t arg, callback_t callback)
    {
        std::string file = first_arg_after_command(arg);
        if (file.empty()) {
            report(callback, -1, "CapFileSave need file\n");
            return;
        }
        capturing_ = false;
        cap_paused_ = false;
        int ret = write_silent_wav(file);
        report(callback, ret, ret == 0 ? "cap file saved\n" : "cap file save failed\n");
    }

    void SetCallback(arg_t arg, callback_t callback)
    {
        (void)arg;
        status_callback_ = std::move(callback);
    }

    void VolumeRead(arg_t arg, callback_t callback)
    {
        (void)arg;
        report(callback, 0, std::to_string(volume_));
    }

    void VolumeWrite(int value, callback_t callback)
    {
        volume_ = value;
        report(callback, 0, std::to_string(volume_));
    }

    void MuteRead(arg_t arg, callback_t callback)
    {
        (void)arg;
        report(callback, 0, muted_ ? "1" : "0");
    }

    void MuteToggle(arg_t arg, callback_t callback)
    {
        (void)arg;
        muted_ = !muted_;
        report(callback, 0, muted_ ? "1" : "0");
    }

    void SetSystemSoundNames(arg_t arg, callback_t callback)
    {
        auto it = arg.begin();
        if (it != arg.end())
            ++it;
        for (size_t i = 0; i < system_sound_names_.size() && it != arg.end(); ++i, ++it) {
            if (!it->empty())
                system_sound_names_[i] = *it;
        }
        report(callback, 0, "ok");
    }

    void SystemSoundPlay(int index, callback_t callback)
    {
        if (index < 0 || index >= static_cast<int>(system_sound_names_.size())) {
            report(callback, -1, "invalid system sound index\n");
            return;
        }
        if (!system_sound_enabled_) {
            report(callback, 0, "system sound disabled\n");
            return;
        }
        system_play(system_sound_names_[static_cast<size_t>(index)]);
        report(callback, 0, "system sound play\n");
    }

    void SystemSoundSuspend(arg_t arg, callback_t callback)
    {
        (void)arg;
        playing_ = false;
        report(callback, 0, "system sound suspended\n");
    }

    void SystemSoundPrepare(arg_t arg, callback_t callback)
    {
        (void)arg;
        report(callback, 0, "system sound ready\n");
    }

    void SystemSoundEnable(bool enabled, callback_t callback)
    {
        system_sound_enabled_ = enabled;
        report(callback, 0, system_sound_enabled_ ? "1" : "0");
    }
};

using AudioRegistrations = cp0::SignalRegistrationBundle<
    decltype(cp0_signal_audio_play), decltype(cp0_signal_audio_cap),
    decltype(cp0_signal_audio_setup), decltype(cp0_signal_audio_api),
    decltype(cp0_signal_system_play)>;

std::mutex g_audio_mutex;
std::shared_ptr<AudioSystem> g_audio;
AudioRegistrations g_audio_registrations;
cp0::audio::RuntimeLifecycle g_audio_lifecycle;

std::shared_ptr<AudioSystem> current_audio()
{
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    return g_audio;
}

} // namespace

extern "C" void init_audio(void)
{
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    if (!g_audio_lifecycle.begin_init()) return;
    try {
    auto audio = std::make_shared<AudioSystem>();
    if (!g_audio_registrations.replace(
        cp0::bind_signal(cp0_signal_audio_play,
                         [audio](std::string wav) { audio->play(std::move(wav)); }),
        cp0::bind_signal(cp0_signal_audio_cap,
                         [audio](bool enable) { audio->cap(enable); }),
        cp0::bind_signal(cp0_signal_audio_setup,
                         [audio](std::list<std::string> arg,
                                 std::function<void(int, std::string)> callback) {
                             try {
                                 audio->setup(std::move(arg), callback);
                             } catch (...) {
                                 cp0::audio::invoke_callback(
                                     callback, -1, "audio backend failure\n");
                             }
                         }),
        cp0::bind_signal(cp0_signal_audio_api,
                         [audio](std::list<std::string> arg,
                                 std::function<void(int, std::string)> callback) {
                             try {
                                 audio->api_call(std::move(arg), callback);
                             } catch (...) {
                                 cp0::audio::invoke_callback(
                                     callback, -1, "audio backend failure\n");
                             }
                         }),
        cp0::bind_signal(cp0_signal_system_play,
                         [audio](std::string name) { audio->system_play(std::move(name)); }))) {
        g_audio_lifecycle.rollback_init();
        return;
    }
    g_audio = std::move(audio);
    g_audio_lifecycle.commit_init();
    } catch (...) {
        g_audio_registrations.reset();
        g_audio.reset();
        g_audio_lifecycle.rollback_init();
    }
}

extern "C" void deinit_audio(void) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        if (!g_audio_lifecycle.begin_shutdown()) return;
        g_audio_registrations.reset();
        g_audio.reset();
    } catch (...) {
    }
}

extern "C" void hal_audio_init(void)
{
    init_audio();
}

extern "C" void hal_audio_play(const char *path)
{
    init_audio();
    const auto audio = current_audio();
    if (audio) audio->play(path ? path : "");
}

extern "C" void hal_audio_play_sync(const char *path)
{
    hal_audio_play(path);
}

extern "C" void hal_audio_stop(void)
{
    const auto audio = current_audio();
    if (audio) audio->api_call({"PlayEnd"}, nullptr);
}

extern "C" void hal_audio_deinit(void)
{
    deinit_audio();
}
