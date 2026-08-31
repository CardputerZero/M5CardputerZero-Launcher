#include "wizard_fonts.h"

#if LV_USE_FREETYPE
#include "lvgl/src/libs/freetype/lv_freetype.h"
#endif

#include <array>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

constexpr std::array<uint32_t, 5> kFontSizes = {10, 12, 14, 16, 22};

bool file_exists(const std::string &path)
{
    if (path.empty()) return false;
    std::ifstream file(path);
    return file.good();
}

template <std::size_t N>
std::string select_font_path(const char *override_path,
                             const std::array<const char *, N> &candidates)
{
    if (override_path && file_exists(override_path))
        return override_path;
    for (const char *candidate : candidates) {
        if (file_exists(candidate))
            return candidate;
    }
    return {};
}

std::string primary_font_path()
{
    static constexpr std::array<const char *, 8> candidates = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
        "/usr/share/APPLaunch/share/font/DejaVuSans.ttf",
        "/usr/share/APPLaunch/share/font/DejaVuSerif.ttf",
        "./share/font/DejaVuSans.ttf",
        "./share/font/DejaVuSerif.ttf",
        "../APPLaunch/share/font/DejaVuSans.ttf",
        "../APPLaunch/share/font/DejaVuSerif.ttf",
    };
    return select_font_path(std::getenv("LAUNCH_WIZARD_FONT"), candidates);
}

std::string cjk_font_path()
{
    static constexpr std::array<const char *, 8> candidates = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
        "/usr/share/APPLaunch/share/font/NotoSansCJK-Regular.ttc",
        "/usr/share/APPLaunch/share/font/NotoSerifCJK-Regular.ttc",
        "./share/font/NotoSansCJK-Regular.ttc",
        "./share/font/NotoSerifCJK-Regular.ttc",
        "../APPLaunch/share/font/NotoSansCJK-Regular.ttc",
        "../APPLaunch/share/font/NotoSerifCJK-Regular.ttc",
    };
    return select_font_path(std::getenv("LAUNCH_WIZARD_CJK_FONT"), candidates);
}

}  // namespace

namespace launch_wizard {

void WizardFonts::init()
{
    if (initialized_)
        return;
    initialized_ = true;

#if LV_USE_FREETYPE
    const std::string primary_path = primary_font_path();
    const std::string cjk_path = cjk_font_path();
    for (std::size_t i = 0; i < kFontSizes.size(); ++i) {
        if (!primary_path.empty()) {
            primary_[i] = lv_freetype_font_create(
                primary_path.c_str(), LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                kFontSizes[i], LV_FREETYPE_FONT_STYLE_NORMAL);
        }
        if (!cjk_path.empty()) {
            cjk_[i] = lv_freetype_font_create(
                cjk_path.c_str(), LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                kFontSizes[i], LV_FREETYPE_FONT_STYLE_NORMAL);
        }
        if (primary_[i] && cjk_[i])
            primary_[i]->fallback = cjk_[i];
    }
#endif
}

void WizardFonts::release()
{
#if LV_USE_FREETYPE
    for (lv_font_t *&font : primary_) {
        if (font)
            lv_freetype_font_delete(font);
        font = nullptr;
    }
    for (lv_font_t *&font : cjk_) {
        if (font)
            lv_freetype_font_delete(font);
        font = nullptr;
    }
#endif
    initialized_ = false;
}

const lv_font_t *WizardFonts::get(std::size_t index,
                                  const lv_font_t *built_in) const
{
    if (primary_[index]) return primary_[index];
    if (cjk_[index]) return cjk_[index];
    return built_in;
}

const lv_font_t *WizardFonts::xs() const { return get(0, &lv_font_montserrat_10); }
const lv_font_t *WizardFonts::sm() const { return get(1, &lv_font_montserrat_12); }
const lv_font_t *WizardFonts::md() const { return get(2, &lv_font_montserrat_14); }
const lv_font_t *WizardFonts::lg() const { return get(3, &lv_font_montserrat_16); }
const lv_font_t *WizardFonts::xl() const { return get(4, &lv_font_montserrat_22); }

}  // namespace launch_wizard
