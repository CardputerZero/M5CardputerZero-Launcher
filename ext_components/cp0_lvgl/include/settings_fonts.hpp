#pragma once

#include "cp0_font_service.hpp"

namespace settings_fonts {

inline const lv_font_t *sans(uint16_t size,
                             lv_freetype_font_style_t style = LV_FREETYPE_FONT_STYLE_NORMAL)
{
    return cp0_fonts().get("DejaVuSans.ttf", size, style);
}

inline const lv_font_t *serif(uint16_t size,
                              lv_freetype_font_style_t style = LV_FREETYPE_FONT_STYLE_NORMAL)
{
    return cp0_fonts().get("DejaVuSerif.ttf", size, style);
}

inline const lv_font_t *mono(uint16_t size,
                             lv_freetype_font_style_t style = LV_FREETYPE_FONT_STYLE_NORMAL)
{
    return cp0_fonts().get_mono("JetBrainsMono-Regular.ttf", size, style);
}

inline const lv_font_t *cjk_sans(uint16_t size,
                                 lv_freetype_font_style_t style = LV_FREETYPE_FONT_STYLE_NORMAL)
{
    const lv_font_t *font = cp0_fonts().get("NotoSansCJK-Regular.ttc", size, style);
    // Alibaba is already shipped with APPLaunch and keeps Chinese text
    // visible on installations that predate the Noto font package.
    if (font == cp0_fonts().fallback(size))
        font = cp0_fonts().get("AlibabaPuHuiTi-3-55-Regular.ttf", size, style);
    return font;
}

inline const lv_font_t *cjk_serif(uint16_t size,
                                  lv_freetype_font_style_t style = LV_FREETYPE_FONT_STYLE_NORMAL)
{
    const lv_font_t *font = cp0_fonts().get("NotoSerifCJK-Regular.ttc", size, style);
    if (font == cp0_fonts().fallback(size))
        font = cp0_fonts().get("AlibabaPuHuiTi-3-55-Regular.ttf", size, style);
    return font;
}

} // namespace settings_fonts
