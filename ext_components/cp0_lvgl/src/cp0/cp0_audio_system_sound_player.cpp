#include "cp0_audio_system_sound_player.hpp"

#include "hal_lvgl_bsp.h"
#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <future>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace {

constexpr std::size_t kSoundCount = 3;
constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kSampleRate = 48000;
constexpr ma_uint32 kPeriodMilliseconds = 20;
// The first output frames after a PulseAudio stream is opened can be swallowed
// while the sink settles. Let the device warm up before starting short sounds.
constexpr auto kDeviceWarmupDuration = std::chrono::milliseconds(350);

std::string resolve_asset(const std::string &name)
{
    if (name.empty()) return {};
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return name;
    const std::string path = cp0_file_path(name);
    return path.empty() ? name : path;
}

} // namespace

class Cp0SystemSoundPlayer::Impl
{
public:
    Impl()
        : names_{"Ding2.wav", "key_back.wav", "key_back.wav"}
        , worker_(&Impl::worker_loop, this)
    {
    }

    ~Impl()
    {
        std::deque<Command> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            pending.swap(commands_);
        }
        for (Command &command : pending) {
            if (command.callback) {
                try {
                    command.callback(false);
                } catch (...) {
                }
            }
        }
        wake_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    int reload(const std::vector<std::string> &names)
    {
        Command command;
        command.type = CommandType::Reload;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (std::size_t i = 0; i < names_.size() && i < names.size(); ++i)
                if (!names[i].empty()) names_[i] = names[i];
            command.names = names_;
            commands_.push_back(std::move(command));
        }
        wake_.notify_one();
        return 0;
    }

    bool play_index(std::size_t index, PlayCallback callback)
    {
        if (!enabled_.load(std::memory_order_acquire) || index >= kSoundCount)
            return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || !enabled_.load(std::memory_order_relaxed)) return false;
            Command command;
            command.type = CommandType::Play;
            command.index = index;
            command.callback = std::move(callback);
            commands_.push_back(std::move(command));
        }
        wake_.notify_one();
        return true;
    }

    bool play_named(const std::string &name)
    {
        std::size_t index = kSoundCount;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = std::find(names_.begin(), names_.end(), name);
            if (it != names_.end()) index = static_cast<std::size_t>(it - names_.begin());
        }
        return index < kSoundCount && play_index(index, nullptr);
    }

    void suspend()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            Command command;
            command.type = CommandType::Suspend;
            commands_.push_back(std::move(command));
        }
        wake_.notify_one();
    }

    bool prepare()
    {
        if (!enabled_.load(std::memory_order_acquire)) return true;
        auto completion = std::make_shared<std::promise<bool>>();
        auto result = completion->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return false;
            Command command;
            command.type = CommandType::Prepare;
            command.callback = [completion](bool ready) {
                try {
                    completion->set_value(ready);
                } catch (...) {
                }
            };
            commands_.push_back(std::move(command));
        }
        wake_.notify_one();
        return result.get();
    }

    bool contains(const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::find(names_.begin(), names_.end(), name) != names_.end();
    }

    void set_enabled(bool enabled)
    {
        enabled_.store(enabled, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Command command;
            command.type = CommandType::SetEnabled;
            commands_.push_back(std::move(command));
        }
        wake_.notify_one();
    }

    bool enabled() const
    {
        return enabled_.load(std::memory_order_acquire);
    }

private:
    enum class CommandType {
        Play,
        Reload,
        SetEnabled,
        Suspend,
        Prepare,
    };

    struct Command {
        CommandType type = CommandType::Play;
        std::size_t index = 0;
        std::array<std::string, kSoundCount> names;
        PlayCallback callback;
    };

    struct CachedSound {
        std::vector<float> pcm;
        ma_audio_buffer buffer{};
        bool buffer_initialized = false;
    };

    void worker_loop()
    {
        std::array<std::string, kSoundCount> initial_names;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            initial_names = names_;
        }
        decode_cache(initial_names);

        for (;;) {
            Command command;
            bool have_command = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                for (;;) {
                    if (stopping_) break;
                    if (!commands_.empty()) {
                        command = std::move(commands_.front());
                        commands_.pop_front();
                        have_command = true;
                        break;
                    }

                    wake_.wait(lock);
                }
                if (stopping_) break;
            }

            if (!have_command) continue;

            switch (command.type) {
            case CommandType::Play:
            {
                const bool played = enabled_.load(std::memory_order_acquire) &&
                                    play(command.index);
                if (command.callback) {
                    try {
                        command.callback(played);
                    } catch (...) {
                    }
                }
                break;
            }
            case CommandType::Reload:
                close_engine();
                decode_cache(command.names);
                break;
            case CommandType::SetEnabled:
                if (!enabled_.load(std::memory_order_acquire)) close_engine();
                break;
            case CommandType::Suspend:
                close_engine();
                break;
            case CommandType::Prepare:
            {
                const bool ready = open_engine();
                if (ready) wait_for_engine_warmup();
                if (command.callback) {
                    try {
                        command.callback(ready);
                    } catch (...) {
                    }
                }
                break;
            }
            }
        }

        close_engine();
        clear_cache();
    }

    bool decode_sound(CachedSound &cached, const std::string &name)
    {
        const std::string path = resolve_asset(name);
        if (path.empty()) return false;

        ma_decoder_config decoder_config =
            ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
        ma_uint64 frame_count = 0;
        void *decoded = nullptr;
        if (ma_decode_file(path.c_str(), &decoder_config, &frame_count, &decoded) != MA_SUCCESS ||
            !decoded || frame_count == 0 ||
            frame_count > std::numeric_limits<std::size_t>::max() / kChannels) {
            if (decoded) ma_free(decoded, nullptr);
            return false;
        }

        const auto *samples = static_cast<const float *>(decoded);
        cached.pcm.assign(samples, samples + static_cast<std::size_t>(frame_count) * kChannels);
        ma_free(decoded, nullptr);

        ma_audio_buffer_config buffer_config = ma_audio_buffer_config_init(
            ma_format_f32, kChannels, frame_count, cached.pcm.data(), nullptr);
        buffer_config.sampleRate = kSampleRate;
        if (ma_audio_buffer_init(&buffer_config, &cached.buffer) != MA_SUCCESS) {
            cached.pcm.clear();
            return false;
        }
        cached.buffer_initialized = true;
        return true;
    }

    void decode_cache(const std::array<std::string, kSoundCount> &names)
    {
        clear_cache();
        for (std::size_t i = 0; i < cache_.size(); ++i)
            decode_sound(cache_[i], names[i]);
    }

    void clear_cache()
    {
        for (CachedSound &cached : cache_) {
            if (cached.buffer_initialized) ma_audio_buffer_uninit(&cached.buffer);
            cached = {};
        }
    }

    bool open_engine()
    {
        if (engine_initialized_) return true;

        ma_backend backends[] = {ma_backend_pulseaudio};
        if (ma_context_init(backends, 1, nullptr, &context_) != MA_SUCCESS)
            return false;
        context_initialized_ = true;

        ma_engine_config config = ma_engine_config_init();
        config.pContext = &context_;
        config.channels = kChannels;
        config.sampleRate = kSampleRate;
        config.periodSizeInMilliseconds = kPeriodMilliseconds;
        if (ma_engine_init(&config, &engine_) != MA_SUCCESS) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
            return false;
        }
        engine_initialized_ = true;

        for (std::size_t i = 0; i < cache_.size(); ++i) {
            if (!cache_[i].buffer_initialized) continue;
            ma_audio_buffer_seek_to_pcm_frame(&cache_[i].buffer, 0);
            if (ma_sound_init_from_data_source(
                    &engine_, reinterpret_cast<ma_data_source *>(&cache_[i].buffer),
                    MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &sounds_[i]) == MA_SUCCESS)
                sound_initialized_[i] = true;
        }

        const bool ready = std::any_of(
            sound_initialized_.begin(), sound_initialized_.end(),
            [](bool initialized) { return initialized; });
        if (!ready) close_engine();
        if (ready) {
            engine_warmup_deadline_ = std::chrono::steady_clock::now() +
                                      kDeviceWarmupDuration;
        }
        return ready;
    }

    void wait_for_engine_warmup()
    {
        if (!engine_initialized_ || engine_warmup_deadline_ ==
                                        std::chrono::steady_clock::time_point{})
            return;
        std::this_thread::sleep_until(engine_warmup_deadline_);
        engine_warmup_deadline_ = {};
    }

    bool play(std::size_t index)
    {
        if (index >= kSoundCount || !cache_[index].buffer_initialized || !open_engine() ||
            !sound_initialized_[index])
            return false;

        wait_for_engine_warmup();
        ma_sound &sound = sounds_[index];
        if (ma_sound_is_playing(&sound)) return true;
        if (ma_sound_seek_to_pcm_frame(&sound, 0) == MA_SUCCESS &&
            ma_sound_start(&sound) == MA_SUCCESS) {
            return true;
        }
        return false;
    }

    void close_engine()
    {
        if (!engine_initialized_) return;
        for (std::size_t i = 0; i < sounds_.size(); ++i) {
            if (sound_initialized_[i]) ma_sound_uninit(&sounds_[i]);
            sound_initialized_[i] = false;
        }
        ma_engine_uninit(&engine_);
        engine_initialized_ = false;
        if (context_initialized_) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
        }
        engine_warmup_deadline_ = {};
    }

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    std::array<std::string, kSoundCount> names_;
    std::atomic<bool> enabled_{true};
    bool stopping_ = false;
    std::thread worker_;

    std::array<CachedSound, kSoundCount> cache_;
    ma_context context_{};
    ma_engine engine_{};
    std::array<ma_sound, kSoundCount> sounds_{};
    std::array<bool, kSoundCount> sound_initialized_{};
    bool context_initialized_ = false;
    bool engine_initialized_ = false;
    std::chrono::steady_clock::time_point engine_warmup_deadline_{};
};

Cp0SystemSoundPlayer::Cp0SystemSoundPlayer()
    : impl_(std::make_unique<Impl>())
{
}

Cp0SystemSoundPlayer::~Cp0SystemSoundPlayer() = default;

int Cp0SystemSoundPlayer::reload(const std::vector<std::string> &names)
{
    return impl_->reload(names);
}

bool Cp0SystemSoundPlayer::play_index(std::size_t index, PlayCallback callback)
{
    return impl_->play_index(index, std::move(callback));
}

bool Cp0SystemSoundPlayer::play_named(const std::string &name)
{
    return impl_->play_named(name);
}

void Cp0SystemSoundPlayer::suspend()
{
    impl_->suspend();
}

bool Cp0SystemSoundPlayer::prepare()
{
    return impl_->prepare();
}

bool Cp0SystemSoundPlayer::contains(const std::string &name) const
{
    return impl_->contains(name);
}

void Cp0SystemSoundPlayer::set_enabled(bool enabled)
{
    impl_->set_enabled(enabled);
}

bool Cp0SystemSoundPlayer::enabled() const
{
    return impl_->enabled();
}

std::size_t Cp0SystemSoundPlayer::sound_count() const
{
    return kSoundCount;
}
