#include "../main/ui/model/global_hint_policy.hpp"

#include "input_keys.h"

#include <cassert>
#include <string>

#ifndef KEY_RIGHTSHIFT
#define KEY_RIGHTSHIFT 54
#endif
#ifndef KEY_COMPOSE
#define KEY_COMPOSE 127
#endif
#ifndef KEY_FN
#define KEY_FN 0x1d0
#endif
#ifndef KEY_MUTE
#define KEY_MUTE 113
#endif
#ifndef KEY_VOLUMEUP
#define KEY_VOLUMEUP 115
#endif
#ifndef KEY_BRIGHTNESSDOWN
#define KEY_BRIGHTNESSDOWN 224
#endif
#ifndef KEY_SYSRQ
#define KEY_SYSRQ 99
#endif
#ifndef KEY_PRINT
#define KEY_PRINT 210
#endif

int main()
{
    const GlobalHintPolicy policy;

    assert(policy.action_for({KEY_BRIGHTNESSDOWN, nullptr, true}) ==
           GlobalHintAction::BRIGHTNESS_DOWN);
    assert(policy.action_for({KEY_VOLUMEUP, nullptr, false, true}) ==
           GlobalHintAction::VOLUME_UP);
    assert(policy.action_for({KEY_MUTE, nullptr, false, true}) == GlobalHintAction::NONE);
    assert(policy.action_for({KEY_MUTE, nullptr, true}) == GlobalHintAction::TOGGLE_MUTE);

    assert(policy.action_for({KEY_S, nullptr, true, false, true, true}) ==
           GlobalHintAction::TAKE_SCREENSHOT);
    assert(policy.action_for({KEY_S, nullptr, true, false, true, false}) ==
           GlobalHintAction::NONE);
    assert(policy.action_for({KEY_S, nullptr, false, true, true, true}) ==
           GlobalHintAction::NONE);
    assert(policy.action_for({KEY_SYSRQ, "Print", true}) ==
           GlobalHintAction::TAKE_SCREENSHOT);
    assert(policy.action_for({KEY_PRINT, "Print", true}) ==
           GlobalHintAction::TAKE_SCREENSHOT);
    assert(policy.action_for({KEY_SYSRQ, "Print", false}) == GlobalHintAction::NONE);

    assert(policy.action_for({KEY_LEFTSHIFT, nullptr, true}) == GlobalHintAction::NONE);
    assert(policy.action_for({KEY_RIGHTSHIFT, nullptr, true}) == GlobalHintAction::NONE);
    assert(policy.action_for({KEY_COMPOSE, nullptr, true}) ==
           GlobalHintAction::SHOW_LOCK_HINT);
    assert(policy.action_for({999, "Multi_key", true}) == GlobalHintAction::SHOW_LOCK_HINT);
    assert(policy.action_for({999, "SYM", true}) == GlobalHintAction::SHOW_LOCK_HINT);
    assert(policy.action_for({999, "sym", true}) == GlobalHintAction::NONE);
    assert(policy.action_for({999, "SYM", false}) == GlobalHintAction::NONE);
    assert(policy.action_for({KEY_FN, "SYM", true}) == GlobalHintAction::NONE);
    assert(policy.action_for({999, nullptr, true}) == GlobalHintAction::NONE);

    assert(GlobalHintScreenshotPolicy::should_save(0));
    assert(!GlobalHintScreenshotPolicy::should_save(-1));
    assert(std::string(GlobalHintScreenshotPolicy::result_message(0, 0)) ==
           "Saved to ~/Pictures/Screenshots");
    assert(std::string(GlobalHintScreenshotPolicy::result_message(-1, 0)) ==
           "Screenshot failed");
    assert(std::string(GlobalHintScreenshotPolicy::result_message(0, -1)) ==
           "Screenshot failed");
    assert(GlobalHintScreenshotPolicy::saved_file_message(
               "/home/test/Pictures/Screenshots/scr_20260727_151000.bmp", "/home/test") ==
           "Saved: scr_20260727_151000.bmp\n~/Pictures/Screenshots");
    assert(GlobalHintScreenshotPolicy::saved_file_message("capture.bmp", "/home/test") ==
           "Screenshot saved\ncapture.bmp");
}
