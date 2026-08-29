/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "home_icon_buffer_pool.hpp"

#include "lvgl/src/draw/lv_image_decoder_private.h"
#include "sample_log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace {

uint8_t rounded_byte(float value)
{
    return static_cast<uint8_t>(std::clamp(value + 0.5f, 0.0f, 255.0f));
}

float rounded_rect_coverage(uint32_t x, uint32_t y)
{
    constexpr float radius = HomeIconBufferPool::kCornerRadius;
    constexpr float size = static_cast<float>(HomeIconBufferPool::kIconSize);
    const float px = static_cast<float>(x) + 0.5f;
    const float py = static_cast<float>(y) + 0.5f;
    const float nearest_x = std::clamp(px, radius, size - radius);
    const float nearest_y = std::clamp(py, radius, size - radius);
    const float dx = px - nearest_x;
    const float dy = py - nearest_y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    return std::clamp(radius + 0.5f - distance, 0.0f, 1.0f);
}

lv_color32_t sample_bilinear(const lv_draw_buf_t &source, uint32_t output_x,
                             uint32_t output_y)
{
    const float source_x =
        (static_cast<float>(output_x) + 0.5f) * source.header.w /
        HomeIconBufferPool::kIconSize - 0.5f;
    const float source_y =
        (static_cast<float>(output_y) + 0.5f) * source.header.h /
        HomeIconBufferPool::kIconSize - 0.5f;
    const int32_t x0_unclamped = static_cast<int32_t>(std::floor(source_x));
    const int32_t y0_unclamped = static_cast<int32_t>(std::floor(source_y));
    const int32_t x0 = std::clamp<int32_t>(x0_unclamped, 0, source.header.w - 1);
    const int32_t y0 = std::clamp<int32_t>(y0_unclamped, 0, source.header.h - 1);
    const int32_t x1 = std::min<int32_t>(x0_unclamped + 1, source.header.w - 1);
    const int32_t y1 = std::min<int32_t>(y0_unclamped + 1, source.header.h - 1);
    const float tx = std::clamp(source_x - x0_unclamped, 0.0f, 1.0f);
    const float ty = std::clamp(source_y - y0_unclamped, 0.0f, 1.0f);

    const auto *row0 = static_cast<const lv_color32_t *>(lv_draw_buf_goto_xy(&source, 0, y0));
    const auto *row1 = static_cast<const lv_color32_t *>(lv_draw_buf_goto_xy(&source, 0, y1));
    const lv_color32_t pixels[4] = {row0[x0], row0[x1], row1[x0], row1[x1]};
    const float weights[4] = {
        (1.0f - tx) * (1.0f - ty), tx * (1.0f - ty),
        (1.0f - tx) * ty, tx * ty,
    };

    float alpha = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    for (size_t i = 0; i < 4; ++i) {
        const float weighted_alpha = weights[i] * pixels[i].alpha;
        alpha += weighted_alpha;
        red += weighted_alpha * pixels[i].red;
        green += weighted_alpha * pixels[i].green;
        blue += weighted_alpha * pixels[i].blue;
    }

    lv_color32_t result{};
    result.alpha = rounded_byte(alpha);
    if (alpha > 0.0f) {
        result.red = rounded_byte(red / alpha);
        result.green = rounded_byte(green / alpha);
        result.blue = rounded_byte(blue / alpha);
    }
    return result;
}

void apply_rounded_corners(lv_draw_buf_t &buffer)
{
    for (uint32_t y = 0; y < buffer.header.h; ++y) {
        auto *row = static_cast<lv_color32_t *>(lv_draw_buf_goto_xy(&buffer, 0, y));
        for (uint32_t x = 0; x < buffer.header.w; ++x)
            row[x].alpha = rounded_byte(row[x].alpha * rounded_rect_coverage(x, y));
    }
}

} // namespace

void HomeIconBufferPool::DrawBufferDeleter::operator()(lv_draw_buf_t *buffer) const noexcept
{
    if (buffer) lv_draw_buf_destroy(buffer);
}

void HomeIconBufferPool::rebuild(const std::vector<std::string> &icon_paths)
{
    icons_.clear();
    fallback_ = create_fallback();
    icons_.reserve(icon_paths.size());

    for (const std::string &path : icon_paths) {
        if (path.empty() || icons_.find(path) != icons_.end()) continue;
        DrawBufferPtr icon = decode_and_prepare(path);
        if (!icon) {
            SLOGW("[HOME] failed to preload icon: %s", path.c_str());
            continue;
        }
        icons_.emplace(path, std::move(icon));
    }
    SLOGI("[HOME] preloaded %zu/%zu unique icons", icons_.size(), icon_paths.size());
}

const lv_image_dsc_t *HomeIconBufferPool::find(const std::string &path) const
{
    const auto found = icons_.find(path);
    return found == icons_.end() ? as_image(fallback_) : as_image(found->second);
}

void HomeIconBufferPool::swap(HomeIconBufferPool &other) noexcept
{
    icons_.swap(other.icons_);
    fallback_.swap(other.fallback_);
}

HomeIconBufferPool::DrawBufferPtr HomeIconBufferPool::create_fallback()
{
    DrawBufferPtr buffer(
        lv_draw_buf_create(kIconSize, kIconSize, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO));
    if (!buffer) return {};

    for (uint32_t y = 0; y < kIconSize; ++y) {
        auto *row = static_cast<lv_color32_t *>(lv_draw_buf_goto_xy(buffer.get(), 0, y));
        for (uint32_t x = 0; x < kIconSize; ++x)
            row[x] = lv_color32_make(0x44, 0x44, 0x44, 0xFF);
    }
    apply_rounded_corners(*buffer);
    return buffer;
}

HomeIconBufferPool::DrawBufferPtr
HomeIconBufferPool::decode_and_prepare(const std::string &path)
{
    lv_image_decoder_dsc_t decoder{};
    lv_image_decoder_args_t args{};
    args.no_cache = true;
    if (lv_image_decoder_open(&decoder, path.c_str(), &args) != LV_RESULT_OK)
        return {};

    const lv_draw_buf_t *source = decoder.decoded;
    if (!source || source->header.w == 0 || source->header.h == 0 ||
        source->header.cf != LV_COLOR_FORMAT_ARGB8888) {
        lv_image_decoder_close(&decoder);
        return {};
    }

    DrawBufferPtr output(
        lv_draw_buf_create(kIconSize, kIconSize, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO));
    if (output) {
        for (uint32_t y = 0; y < kIconSize; ++y) {
            auto *row = static_cast<lv_color32_t *>(lv_draw_buf_goto_xy(output.get(), 0, y));
            for (uint32_t x = 0; x < kIconSize; ++x)
                row[x] = sample_bilinear(*source, x, y);
        }
        apply_rounded_corners(*output);
    }
    lv_image_decoder_close(&decoder);
    return output;
}

const lv_image_dsc_t *HomeIconBufferPool::as_image(const DrawBufferPtr &buffer)
{
    return reinterpret_cast<const lv_image_dsc_t *>(buffer.get());
}
