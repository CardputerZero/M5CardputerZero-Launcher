#pragma once

#include "lvgl/lvgl.h"

#include <array>
#include <cstddef>

namespace launch_wizard {

class WizardFonts {
public:
    void init();
    void release();

    const lv_font_t *xs() const;
    const lv_font_t *sm() const;
    const lv_font_t *md() const;
    const lv_font_t *lg() const;
    const lv_font_t *xl() const;

private:
    const lv_font_t *get(std::size_t index, const lv_font_t *built_in) const;

    bool initialized_ = false;
    std::array<lv_font_t *, 5> primary_{};
    std::array<lv_font_t *, 5> cjk_{};
};

}  // namespace launch_wizard
