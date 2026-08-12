#include "zclaw_sound_effects.h"

#include "cp0_lvgl_file.hpp"
#include "hal_lvgl_bsp.h"

#include <string>

namespace zclaw {

SoundEffects::~SoundEffects()
{
    if (configured_)
        cp0_signal_audio_api({"SystemSoundEnable", "0"}, nullptr);
}

void SoundEffects::configure(bool enabled)
{
    cp0_signal_audio_api(
        {"SetSystemSoundNames",
         cp0_file_path("share/audio/zclaw-scifi-send.mp3"),
         cp0_file_path("share/audio/zclaw-scifi-receive.mp3"),
         cp0_file_path("share/audio/zclaw-scifi-error.mp3")},
        nullptr);
    configured_ = true;
    set_enabled(enabled);
}

void SoundEffects::set_enabled(bool enabled)
{
    enabled_ = enabled;
    if (!configured_)
        return;
    cp0_signal_audio_api(
        {"SystemSoundEnable", enabled ? "1" : "0"}, nullptr);
}

bool SoundEffects::enabled() const
{
    return enabled_;
}

void SoundEffects::play(SoundCue cue) const
{
    if (!configured_ || !enabled_)
        return;
    cp0_signal_audio_api(
        {"SystemSoundPlay", std::to_string(static_cast<int>(cue))}, nullptr);
}

}  // namespace zclaw
