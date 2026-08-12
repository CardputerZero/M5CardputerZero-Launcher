/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

enum class GlobalHintAction {
    NONE,
    BRIGHTNESS_UP,
    BRIGHTNESS_DOWN,
    VOLUME_UP,
    VOLUME_DOWN,
    TOGGLE_MUTE,
    TAKE_SCREENSHOT,
    SHOW_LOCK_HINT,
};

struct GlobalHintKeyInput {
    uint32_t key_code = 0;
    const char *symbol_name = nullptr;
    bool pressed = false;
    bool repeated = false;
    bool control = false;
    bool alt = false;
};

class GlobalHintPolicy
{
public:
    GlobalHintAction action_for(const GlobalHintKeyInput &input) const;
};

class GlobalHintScreenshotPolicy
{
public:
    static bool should_save(int ensure_directory_code)
    {
        return ensure_directory_code == 0;
    }

    static const char *result_message(int ensure_directory_code, int save_code)
    {
        return should_save(ensure_directory_code) && save_code == 0
            ? "Saved to ~/Pictures/Screenshots" : "Screenshot failed";
    }

    static std::string saved_file_message(const std::string &path,
                                          const std::string &home_directory);
};
