#include "hal_lvgl_bsp.h"
#include "../cp0_audio_api_contract.hpp"
#include "cp0_audio_capture.hpp"
#include "cp0_audio_mixer_control.hpp"
#include "cp0_audio_system_sound_player.hpp"
#include "../cp0_signal_registration.hpp"
#include "../cp0_audio_runtime_lifecycle.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"


class AudioSystem
{
public:
    typedef std::function<void(int, std::string)> callback_t;
    typedef std::list<std::string> arg_t;

    AudioSystem()
        : playback_cleanup_thread_(&AudioSystem::playback_cleanup_loop, this)
    {
    }
    ~AudioSystem()
    {
        shutdown();
    }

    void shutdown() noexcept
    {
        if (shutdown_started_.exchange(true))
            return;
        try { set_status_callback(nullptr); } catch (...) {}
        try { capture_.set_status_callback(nullptr); } catch (...) {}
        try { capture_.stop(); } catch (...) {}
        playback_cleanup_stop_.store(true);
        playback_cleanup_cv_.notify_one();
        if (playback_cleanup_thread_.joinable())
            playback_cleanup_thread_.join();
        try { stop_play_device(false); } catch (...) {}
    }

public:
    std::function<void(int, std::string)> _cap_status_callback;
    std::mutex callback_mutex_;
    std::unique_ptr<ma_device>  ma_cp0_play_device;
    std::unique_ptr<ma_decoder> ma_cp0_play_decoder;
    std::atomic<bool> play_finished_reported_{false};
    std::atomic<bool> playback_stopping_{false};
    std::mutex playback_mutex_;
    std::atomic<std::uint64_t> playback_generation_{0};
    std::atomic<std::uint64_t> playback_cleanup_generation_{0};
    std::atomic<bool> playback_cleanup_stop_{false};
    std::atomic<bool> shutdown_started_{false};
    std::mutex playback_cleanup_wait_mutex_;
    std::condition_variable playback_cleanup_cv_;
    std::thread playback_cleanup_thread_;

    Cp0SystemSoundPlayer system_sounds_;
    Cp0AudioMixerControl mixer_;
    Cp0AudioCapture capture_;

    static void play_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
    {
        AudioSystem* self = (AudioSystem*)pDevice->pUserData;
        // ma_decoder* pDecoder = (ma_decoder*)pDevice->pUserData;
        if (self == NULL || self->ma_cp0_play_decoder.get() == NULL) {
            ma_silence_pcm_frames(pOutput, frameCount, pDevice->playback.format, pDevice->playback.channels);
            return;
        }
        ma_uint64 framesRead ;
        ma_result result = ma_decoder_read_pcm_frames(self->ma_cp0_play_decoder.get(), pOutput, frameCount, &framesRead);
        if (framesRead < frameCount) {
            void* silence = ma_offset_pcm_frames_ptr(pOutput, framesRead, pDevice->playback.format, pDevice->playback.channels);
            ma_silence_pcm_frames(silence, frameCount - framesRead, pDevice->playback.format, pDevice->playback.channels);
        }
        bool finished = (result == MA_AT_END || framesRead < frameCount);
        if(finished && !self->playback_stopping_.load() &&
           !self->play_finished_reported_.exchange(true)) {
            self->playback_cleanup_generation_.store(
                self->playback_generation_.load());
            self->playback_cleanup_cv_.notify_one();
            auto callback = self->status_callback();
            cp0::audio::invoke_callback(callback, 0, "play over\n");
        }
        (void)pInput;
    }



    int play(std::string wav)
    {
        std::unique_lock<std::mutex> lock(playback_mutex_);
        ma_result result;
        ma_device_config deviceConfig;
        stop_play_device_locked(false);
        playback_generation_.fetch_add(1);
        playback_cleanup_generation_.store(0);
        play_finished_reported_.store(false);
        ma_cp0_play_device = std::make_unique<ma_device>();
        ma_cp0_play_decoder = std::make_unique<ma_decoder>();
        result = ma_decoder_init_file(wav.c_str(), NULL, ma_cp0_play_decoder.get());
        if (result != MA_SUCCESS) {
            ma_cp0_play_decoder.reset();
            ma_cp0_play_device.reset();
            lock.unlock();
            report_status(-2, "Could not load file\n");
            return -2;
        }

        deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.format   = ma_cp0_play_decoder.get()->outputFormat;
        deviceConfig.playback.channels = ma_cp0_play_decoder.get()->outputChannels;
        deviceConfig.sampleRate        = ma_cp0_play_decoder.get()->outputSampleRate;
        deviceConfig.dataCallback      = play_data_callback;
        deviceConfig.pUserData         = this;

        if (ma_device_init(NULL, &deviceConfig, ma_cp0_play_device.get()) != MA_SUCCESS) {
            ma_decoder_uninit(ma_cp0_play_decoder.get());
            ma_cp0_play_decoder.reset();
            ma_cp0_play_device.reset();
            lock.unlock();
            report_status(-3, "Failed to open playback device.\n");
            return -3;
        }

        if (ma_device_start(ma_cp0_play_device.get()) != MA_SUCCESS) {
            ma_device_uninit(ma_cp0_play_device.get());
            ma_decoder_uninit(ma_cp0_play_decoder.get());
            ma_cp0_play_decoder.reset();
            ma_cp0_play_device.reset();
            lock.unlock();
            report_status(-4, "Failed to start playback device.\n");
            return -4;
        }

        return 0;
    }









private:
    void playback_cleanup_loop()
    {
        std::unique_lock<std::mutex> wait_lock(playback_cleanup_wait_mutex_);
        while (!playback_cleanup_stop_.load()) {
            playback_cleanup_cv_.wait(wait_lock, [this] {
                return playback_cleanup_stop_.load() ||
                       playback_cleanup_generation_.load() != 0;
            });
            if (playback_cleanup_stop_.load())
                break;

            const std::uint64_t generation =
                playback_cleanup_generation_.exchange(0);
            wait_lock.unlock();
            {
                std::lock_guard<std::mutex> lock(playback_mutex_);
                if (generation == playback_generation_.load() &&
                    play_finished_reported_.load())
                    stop_play_device_locked(false);
            }
            wait_lock.lock();
        }
    }

    callback_t status_callback()
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return _cap_status_callback;
    }

    void set_status_callback(callback_t callback)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        _cap_status_callback = std::move(callback);
    }

    void report_status(int code, const std::string &data)
    {
        auto callback = status_callback();
        cp0::audio::invoke_callback(callback, code, data);
    }

    void report(callback_t callback, int code, const std::string& data)
    {
        if (!callback) {
            report_status(code, data);
            return;
        }
        cp0::audio::invoke_callback(callback, code, data);
    }

    static std::string first_arg_after_command(const arg_t& arg)
    {
        if(arg.size() < 2) return "";
        return *std::next(arg.begin());
    }

    static bool has_path_separator(const std::string& path)
    {
        return path.find('/') != std::string::npos || path.find('\\') != std::string::npos;
    }

    static std::string resolve_play_file(const std::string& file, bool asset)
    {
        if(file.empty()) return "";
        if(!asset || has_path_separator(file)) return file;

        std::string path = cp0_file_path(file);
        return path.empty() ? file : path;
    }

    void stop_play_device(bool report_state)
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        stop_play_device_locked(report_state);
    }

    void stop_play_device_locked(bool report_state)
    {
        playback_stopping_.store(true);
        playback_cleanup_generation_.store(0);
        play_finished_reported_.store(false);
        if(ma_cp0_play_device)
        {
            ma_device_uninit(ma_cp0_play_device.get());
            ma_cp0_play_device.reset();
        }
        if(ma_cp0_play_decoder)
        {
            ma_decoder_uninit(ma_cp0_play_decoder.get());
            ma_cp0_play_decoder.reset();
        }
        playback_stopping_.store(false);
        if(report_state) report_status(0, "play stop\n");
    }

public:
    void system_play(std::string name)
    {
        if(system_sounds_.contains(name)) {
            system_sounds_.play_named(name);
            return;
        }
        std::string file = resolve_play_file(name, true);
        if(!file.empty() && system_sounds_.enabled()) play(file);
    }

    void SetSystemSoundNames(arg_t arg, callback_t callback)
    {
        std::vector<std::string> names;
        auto it = arg.begin();
        if(it != arg.end()) ++it;
        for(; it != arg.end(); ++it) names.push_back(*it);
        int ret = system_sounds_.reload(names);
        report(callback, ret, ret == 0 ? "ok" : "system sound reload failed\n");
    }

    void SystemSoundPlay(int index, callback_t callback)
    {
        if(index < 0 || index >= static_cast<int>(system_sounds_.sound_count()))
        {
            report(callback, -1, "invalid system sound index\n");
            return;
        }
        if(!system_sounds_.enabled())
        {
            report(callback, 0, "system sound disabled\n");
            return;
        }
        bool played = system_sounds_.play_index(static_cast<size_t>(index));
        report(callback, played ? 0 : -2, played ? "system sound play\n" : "system sound play failed\n");
    }

    void SystemSoundEnable(bool enabled, callback_t callback)
    {
        system_sounds_.set_enabled(enabled);
        report(callback, 0, system_sounds_.enabled() ? "1" : "0");
    }

    void cap(bool enable)
    {
        if(enable)
        {
            capture_.start();
        }else{
            capture_.stop();
        }
    }
    void setup(std::list<std::string> arg, std::function<void(int, std::string)> callback)
    {
        cp0::audio::SetupRequest request;
        if (!cp0::audio::parse_setup_request(arg, request)) {
            report(callback, -1, cp0::audio::invalid_setup_request_message());
            return;
        }
        if(request.command == cp0::audio::SetupCommand::SetCallback) {
            set_status_callback(callback);
            capture_.set_status_callback(callback);
        } else if(request.command == cp0::audio::SetupCommand::SetWaveform) {
            capture_.set_waveform_enabled(request.waveform_enabled);
        } else {
            stop_play_device(false);
        }
    }
    // Recording control: start, pause, resume, and stop with save.
    // Playback control: start, pause, resume, and stop.
    void PlayFile(arg_t arg, callback_t callback)
    {
        std::string file = resolve_play_file(first_arg_after_command(arg), false);
        if(file.empty())
        {
            report(callback, -1, "PlayFile need file\n");
            return;
        }
        int ret = play(file);
        report(callback, ret, ret == 0 ? "play start\n" : "play failed\n");
    }

    void Play(arg_t arg, callback_t callback)
    {
        std::string file = resolve_play_file(first_arg_after_command(arg), true);
        if(file.empty())
        {
            report(callback, -1, "Play need file\n");
            return;
        }
        int ret = play(file);
        report(callback, ret, ret == 0 ? "play start\n" : "play failed\n");
    }

    void PlayPause(arg_t arg, callback_t callback)
    {
        (void)arg;
        ma_result result = MA_INVALID_OPERATION;
        bool started = false;
        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            if(ma_cp0_play_device) {
                started = true;
                result = ma_device_stop(ma_cp0_play_device.get());
            }
        }
        if(!started)
            report(callback, -1, "play not started\n");
        else
            report(callback, result == MA_SUCCESS ? 0 : -2,
                   result == MA_SUCCESS ? "play pause\n" : "play pause failed\n");
    }

    void PlayContinue(arg_t arg, callback_t callback)
    {
        (void)arg;
        ma_result result = MA_INVALID_OPERATION;
        bool started = false;
        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            if(ma_cp0_play_device) {
                started = true;
                result = ma_device_start(ma_cp0_play_device.get());
            }
        }
        if(!started)
            report(callback, -1, "play not started\n");
        else
            report(callback, result == MA_SUCCESS ? 0 : -2,
                   result == MA_SUCCESS ? "play continue\n" : "play continue failed\n");
    }

    void PlayEnd(arg_t arg, callback_t callback)
    {
        (void)arg;
        stop_play_device(false);
        report(callback, 0, "play stop\n");
    }

    void Cap(arg_t arg, callback_t callback)
    {
        (void)arg;
        int ret = capture_.start();
        report(callback, ret, ret == 0 ? "cap start\n" : "cap failed\n");
    }

    void CapPause(arg_t arg, callback_t callback)
    {
        (void)arg;
        if(!capture_.active())
        {
            report(callback, -1, "cap not started\n");
            return;
        }
        int ret = capture_.pause();
        report(callback, ret, ret == 0 ? "cap pause\n" : "cap pause failed\n");
    }

    void CapContinue(arg_t arg, callback_t callback)
    {
        (void)arg;
        if(!capture_.active())
        {
            report(callback, -1, "cap not started\n");
            return;
        }
        int ret = capture_.resume();
        report(callback, ret, ret == 0 ? "cap continue\n" : "cap continue failed\n");
    }

    void CapEnd(arg_t arg, callback_t callback)
    {
        (void)arg;
        capture_.stop();
        report(callback, 0, "cap stop\n");
    }

    void CapFileSave(arg_t arg, callback_t callback)
    {
        std::string file = first_arg_after_command(arg);
        if(file.empty())
        {
            report(callback, -1, "CapFileSave need file\n");
            return;
        }
        int ret = capture_.save(file);
        report(callback, ret, ret == 0 ? "cap file saved\n" : "cap file save failed\n");
    }

    void SetCallback(arg_t arg, callback_t callback)
    {
        (void)arg;
        set_status_callback(callback);
        capture_.set_status_callback(callback);
    }

    void VolumeRead(arg_t arg, callback_t callback)
    {
        (void)arg;
        int val = mixer_.read_volume();
        report(callback, val >= 0 ? 0 : -1, std::to_string(val));
    }

    void VolumeWrite(int val, callback_t callback)
    {
        int ret = mixer_.write_volume(val);
        report(callback, ret >= 0 ? 0 : -1, std::to_string(ret));
    }

    void MuteRead(arg_t arg, callback_t callback)
    {
        (void)arg;
        int val = mixer_.read_mute();
        report(callback, val >= 0 ? 0 : -1, std::to_string(val));
    }

    void MuteToggle(arg_t arg, callback_t callback)
    {
        (void)arg;
        int val = mixer_.toggle_mute();
        report(callback, val >= 0 ? 0 : -1, std::to_string(val));
    }

    void api_call(arg_t arg, callback_t callback)
    {
        cp0::audio::ApiRequest request;
        if(!cp0::audio::parse_api_request(arg, request)) {
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

        if(request.command == cp0::audio::ApiCommand::VolumeWrite) {
            VolumeWrite(request.value, callback);
            return;
        }
        if(request.command == cp0::audio::ApiCommand::SystemSoundPlay) {
            SystemSoundPlay(request.value, callback);
            return;
        }
        if(request.command == cp0::audio::ApiCommand::SystemSoundEnable) {
            SystemSoundEnable(request.enabled, callback);
            return;
        }

        for (const auto& it : cmd_map)
        {
            if (it.first == arg.front())
            {
                it.second(arg, callback);
                return;
            }
        }
        report(callback, -1, "unknown audio api\n");
    }

};

namespace {

using AudioRegistrations = cp0::SignalRegistrationBundle<
    decltype(cp0_signal_audio_play), decltype(cp0_signal_audio_cap),
    decltype(cp0_signal_audio_setup), decltype(cp0_signal_audio_api),
    decltype(cp0_signal_system_play)>;

std::mutex g_audio_mutex;
std::shared_ptr<AudioSystem> g_audio;
AudioRegistrations g_audio_registrations;
cp0::audio::RuntimeLifecycle g_audio_lifecycle;

} // namespace

extern "C" void init_audio(void)
{
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    if (!g_audio_lifecycle.begin_init()) return;
    try {
    auto active_audio = std::make_shared<AudioSystem>();
    if (!g_audio_registrations.replace(
        cp0::bind_signal(cp0_signal_audio_play,
                         [active_audio](std::string wav) { active_audio->play(std::move(wav)); }),
        cp0::bind_signal(cp0_signal_audio_cap,
                         [active_audio](bool enable) { active_audio->cap(enable); }),
        cp0::bind_signal(cp0_signal_audio_setup,
                         [active_audio](std::list<std::string> arg,
                                 std::function<void(int, std::string)> callback) {
                             try {
                                 active_audio->setup(std::move(arg), callback);
                             } catch (...) {
                                 cp0::audio::invoke_callback(
                                     callback, -1, "audio backend failure\n");
                             }
                         }),
        cp0::bind_signal(cp0_signal_audio_api,
                         [active_audio](std::list<std::string> arg,
                                 std::function<void(int, std::string)> callback) {
                             try {
                                 active_audio->api_call(std::move(arg), callback);
                             } catch (...) {
                                 cp0::audio::invoke_callback(
                                     callback, -1, "audio backend failure\n");
                             }
                         }),
        cp0::bind_signal(cp0_signal_system_play,
                         [active_audio](std::string name) {
                             active_audio->system_play(std::move(name));
                         }))) {
        g_audio_lifecycle.rollback_init();
        return;
    }
    g_audio = std::move(active_audio);
    g_audio_lifecycle.commit_init();
    } catch (...) {
        g_audio_registrations.reset();
        g_audio.reset();
        g_audio_lifecycle.rollback_init();
    }
}

extern "C" void deinit_audio(void) noexcept
{
    std::shared_ptr<AudioSystem> audio;
    try {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        if (!g_audio_lifecycle.begin_shutdown()) return;
        g_audio_registrations.reset();
        audio = std::move(g_audio);
    } catch (...) {
        return;
    }
    if (audio) audio->shutdown();
}
