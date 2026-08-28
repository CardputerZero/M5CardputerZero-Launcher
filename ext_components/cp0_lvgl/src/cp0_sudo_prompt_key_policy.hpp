#pragma once

namespace cp0_sudo {

class PromptConfirmKeyGate {
public:
    void reset() noexcept { pressed_in_prompt_ = false; }

    void press() noexcept { pressed_in_prompt_ = true; }

    bool release() noexcept
    {
        const bool confirmed = pressed_in_prompt_;
        pressed_in_prompt_ = false;
        return confirmed;
    }

private:
    bool pressed_in_prompt_ = false;
};

} // namespace cp0_sudo
