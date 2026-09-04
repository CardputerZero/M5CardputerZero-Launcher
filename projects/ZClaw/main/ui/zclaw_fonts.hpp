#pragma once

#include "lvgl/lvgl.h"

namespace zclaw {

class FontManager {
public:
    FontManager() = default;
    ~FontManager();
    FontManager(const FontManager &) = delete;
    FontManager &operator=(const FontManager &) = delete;

    void init();
    void release();
    const lv_font_t *font_10() const;
    const lv_font_t *font_12() const;
    const lv_font_t *settings_font_10() const;
    const lv_font_t *settings_font_12() const;

private:
    lv_font_t *font_10_ = nullptr;
    lv_font_t *font_12_ = nullptr;
    lv_font_t *settings_font_10_ = nullptr;
    lv_font_t *settings_font_12_ = nullptr;
    lv_font_t *fallback_font_10_ = nullptr;
    lv_font_t *fallback_font_12_ = nullptr;
};

}  // namespace zclaw
