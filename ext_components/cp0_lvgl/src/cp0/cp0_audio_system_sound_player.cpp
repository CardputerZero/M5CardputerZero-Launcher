#include "cp0_audio_system_sound_player.hpp"

#include "hal_lvgl_bsp.h"
#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

class Cp0SystemSoundPlayer::Impl
{
public:
    static constexpr std::size_t kSoundCount = 3;

    Impl()
        : names{"Ding2.wav", "key_back.wav", "key_back.wav"}
        , cleanup_thread(&Impl::cleanup_loop, this)
    {
    }

    ~Impl()
    {
        cleanup_stop.store(true);
        cleanup_cv.notify_one();
        if (cleanup_thread.joinable())
            cleanup_thread.join();

        std::lock_guard<std::mutex> lock(mutex);
        unload();
        uninitialize();
    }

    static void sound_end_callback(void *user_data, ma_sound *)
    {
        auto *self = static_cast<Impl *>(user_data);
        self->cleanup_requested.store(true);
        self->cleanup_cv.notify_one();
    }

    void cleanup_loop()
    {
        std::unique_lock<std::mutex> wait_lock(cleanup_wait_mutex);
        while (!cleanup_stop.load()) {
            cleanup_cv.wait(wait_lock, [this] {
                return cleanup_stop.load() || cleanup_requested.load();
            });
            if (cleanup_stop.load())
                break;

            cleanup_requested.store(false);
            wait_lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            {
                std::lock_guard<std::mutex> lock(mutex);
                bool playing = false;
                for (std::size_t i = 0; i < slots.size(); ++i)
                    playing = playing ||
                              (slots[i] && ma_sound_is_playing(&sounds[i]));
                if (!playing) {
                    unload();
                    uninitialize();
                }
            }
            wait_lock.lock();
        }
    }

    int initialize()
    {
        if (initialized)
            return 0;
        ma_backend backends[] = {ma_backend_pulseaudio};
        if (ma_context_init(backends, 1, nullptr, &context) != MA_SUCCESS)
            return -1;

        ma_engine_config config = ma_engine_config_init();
        config.pContext = &context;
        config.channels = 2;
        config.sampleRate = 48000;
        if (ma_engine_init(&config, &engine) != MA_SUCCESS) {
            ma_context_uninit(&context);
            return -2;
        }
        initialized = true;
        return 0;
    }

    int load()
    {
        if (loaded)
            return 0;
        int result = initialize();
        if (result != 0)
            return result;

        for (std::size_t i = 0; i < names.size(); ++i) {
            const std::string path = resolve_asset(names[i]);
            if (path.empty())
                continue;
            if (ma_sound_init_from_file(&engine, path.c_str(), MA_SOUND_FLAG_DECODE,
                                        nullptr, nullptr, &sounds[i]) == MA_SUCCESS) {
                ma_sound_set_end_callback(&sounds[i], sound_end_callback, this);
                slots[i] = true;
            }
        }
        loaded = std::any_of(slots.begin(), slots.end(), [](bool slot) { return slot; });
        if (loaded)
            return 0;

        uninitialize();
        return -3;
    }

    void unload()
    {
        for (std::size_t i = 0; i < sounds.size(); ++i) {
            if (slots[i]) {
                ma_sound_uninit(&sounds[i]);
                slots[i] = false;
            }
        }
        loaded = false;
    }

    void uninitialize()
    {
        if (!initialized)
            return;
        ma_engine_uninit(&engine);
        ma_context_uninit(&context);
        initialized = false;
    }

    static std::string resolve_asset(const std::string &name)
    {
        if (name.empty())
            return {};
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
            return name;
        const std::string path = cp0_file_path(name);
        return path.empty() ? name : path;
    }

    ma_context context{};
    ma_engine engine{};
    std::array<ma_sound, kSoundCount> sounds{};
    std::array<bool, kSoundCount> slots{};
    std::array<std::string, kSoundCount> names;
    bool initialized = false;
    bool loaded = false;
    bool enabled = true;
    mutable std::mutex mutex;
    std::atomic<bool> cleanup_requested{false};
    std::atomic<bool> cleanup_stop{false};
    std::mutex cleanup_wait_mutex;
    std::condition_variable cleanup_cv;
    std::thread cleanup_thread;
};

Cp0SystemSoundPlayer::Cp0SystemSoundPlayer()
    : impl_(std::make_unique<Impl>())
{
}

Cp0SystemSoundPlayer::~Cp0SystemSoundPlayer() = default;

int Cp0SystemSoundPlayer::reload(const std::vector<std::string> &names)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (std::size_t i = 0; i < impl_->names.size() && i < names.size(); ++i)
        if (!names[i].empty())
            impl_->names[i] = names[i];
    impl_->unload();
    impl_->uninitialize();
    return 0;
}

bool Cp0SystemSoundPlayer::play_index(std::size_t index)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->enabled || (impl_->load() != 0 && !impl_->loaded) ||
        index >= impl_->sounds.size() || !impl_->slots[index])
        return false;
    ma_sound *sound = &impl_->sounds[index];
    if (ma_sound_is_playing(sound))
        return true;
    return ma_sound_seek_to_pcm_frame(sound, 0) == MA_SUCCESS &&
           ma_sound_start(sound) == MA_SUCCESS;
}

bool Cp0SystemSoundPlayer::play_named(const std::string &name)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->enabled || (impl_->load() != 0 && !impl_->loaded))
        return false;
    for (std::size_t i = 0; i < impl_->names.size(); ++i) {
        if (impl_->names[i] != name || !impl_->slots[i])
            continue;
        ma_sound *sound = &impl_->sounds[i];
        if (ma_sound_is_playing(sound))
            return true;
        return ma_sound_seek_to_pcm_frame(sound, 0) == MA_SUCCESS &&
               ma_sound_start(sound) == MA_SUCCESS;
    }
    return false;
}

bool Cp0SystemSoundPlayer::contains(const std::string &name) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return std::find(impl_->names.begin(), impl_->names.end(), name) != impl_->names.end();
}

void Cp0SystemSoundPlayer::set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->enabled = enabled;
    if (!enabled) {
        impl_->unload();
        impl_->uninitialize();
    }
}

bool Cp0SystemSoundPlayer::enabled() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->enabled;
}

std::size_t Cp0SystemSoundPlayer::sound_count() const
{
    return Impl::kSoundCount;
}
