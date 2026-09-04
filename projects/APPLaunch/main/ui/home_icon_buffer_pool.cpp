/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "home_icon_buffer_pool.hpp"

#include "lvgl/src/draw/lv_image_decoder_private.h"
#include "screensaver_fallback.h"
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

lv_color32_t sample_bilinear(const lv_draw_buf_t &source, uint32_t output_x,
                             uint32_t output_y, uint32_t output_size)
{
    const float source_x =
        (static_cast<float>(output_x) + 0.5f) * source.header.w /
        output_size - 0.5f;
    const float source_y =
        (static_cast<float>(output_y) + 0.5f) * source.header.h /
        output_size - 0.5f;
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

} // namespace

void HomeIconBufferPool::DrawBufferDeleter::operator()(lv_draw_buf_t *buffer) const noexcept
{
    if (buffer) lv_draw_buf_destroy(buffer);
}

void HomeIconBufferPool::rebuild(const std::vector<std::string> &icon_paths)
{
    icons_.clear();
    resized_icons_.clear();
    icons_.reserve(icon_paths.size());

    for (const std::string &path : icon_paths) {
        if (path.empty() || icons_.find(path) != icons_.end()) continue;
        DrawBufferPtr icon = decode_and_prepare(path, kIconSize);
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
    return found == icons_.end() ? &screensaver_fallback : as_image(found->second);
}

const lv_image_dsc_t *HomeIconBufferPool::find(const std::string &path, uint32_t size)
{
    if (size == 0 || size == kIconSize)
        return find(path);

    const auto found = icons_.find(path);
    if (found == icons_.end())
        return &screensaver_fallback;

    const std::string key = path + "\n" + std::to_string(size);
    const auto resized = resized_icons_.find(key);
    if (resized != resized_icons_.end())
        return as_image(resized->second);

    DrawBufferPtr icon = decode_and_prepare(path, size);
    if (!icon)
        return as_image(found->second);
    const lv_image_dsc_t *result = as_image(icon);
    resized_icons_.emplace(key, std::move(icon));
    return result;
}

void HomeIconBufferPool::swap(HomeIconBufferPool &other) noexcept
{
    icons_.swap(other.icons_);
    resized_icons_.swap(other.resized_icons_);
}

HomeIconBufferPool::DrawBufferPtr
HomeIconBufferPool::decode_and_prepare(const std::string &path, uint32_t size)
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
        lv_draw_buf_create(size, size, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO));
    if (output) {
        for (uint32_t y = 0; y < size; ++y) {
            auto *row = static_cast<lv_color32_t *>(lv_draw_buf_goto_xy(output.get(), 0, y));
            for (uint32_t x = 0; x < size; ++x)
                row[x] = sample_bilinear(*source, x, y, size);
        }
    }
    lv_image_decoder_close(&decoder);
    return output;
}

const lv_image_dsc_t *HomeIconBufferPool::as_image(const DrawBufferPtr &buffer)
{
    return reinterpret_cast<const lv_image_dsc_t *>(buffer.get());
}
