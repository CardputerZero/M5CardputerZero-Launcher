#pragma once

namespace zclaw {

enum class SoundCue {
    Send = 0,
    Receive = 1,
    Error = 2,
};

constexpr SoundCue sound_cue_for_result(bool ok)
{
    return ok ? SoundCue::Receive : SoundCue::Error;
}

class SoundEffects {
public:
    SoundEffects() = default;
    ~SoundEffects();

    SoundEffects(const SoundEffects &) = delete;
    SoundEffects &operator=(const SoundEffects &) = delete;

    void configure(bool enabled);
    void set_enabled(bool enabled);
    bool enabled() const;
    void play(SoundCue cue) const;

private:
    bool configured_ = false;
    bool enabled_ = false;
};

}  // namespace zclaw
