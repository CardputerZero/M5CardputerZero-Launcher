#include "terminal_unicode.hpp"

namespace {
constexpr uint32_t kReplacement = 0xfffd;

bool in_range(uint32_t value, uint32_t first, uint32_t last)
{
    return value >= first && value <= last;
}
}

TerminalUtf8Result TerminalUtf8Decoder::feed(uint8_t byte) noexcept
{
    if (remaining_ == 0) {
        if (byte < 0x80) return {true, false, byte};
        if (byte >= 0xc2 && byte <= 0xdf) {
            value_ = byte & 0x1f; minimum_ = 0x80; remaining_ = 1; return {};
        }
        if (byte >= 0xe0 && byte <= 0xef) {
            value_ = byte & 0x0f; minimum_ = 0x800; remaining_ = 2; return {};
        }
        if (byte >= 0xf0 && byte <= 0xf4) {
            value_ = byte & 0x07; minimum_ = 0x10000; remaining_ = 3; return {};
        }
        return {true, false, kReplacement};
    }

    if ((byte & 0xc0) != 0x80) {
        reset();
        return {true, true, kReplacement};
    }
    value_ = (value_ << 6) | (byte & 0x3f);
    if (--remaining_ != 0) return {};
    const uint32_t value = value_;
    const uint32_t minimum = minimum_;
    reset();
    if (value < minimum || value > 0x10ffff || in_range(value, 0xd800, 0xdfff))
        return {true, false, kReplacement};
    return {true, false, value};
}

void TerminalUtf8Decoder::reset() noexcept
{
    value_ = 0;
    minimum_ = 0;
    remaining_ = 0;
}

int terminal_codepoint_width(uint32_t codepoint) noexcept
{
    if (codepoint == 0) return 0;
    if (codepoint < 32 || (codepoint >= 0x7f && codepoint < 0xa0)) return 0;
    if (in_range(codepoint, 0x0300, 0x036f) || in_range(codepoint, 0x1ab0, 0x1aff) ||
        in_range(codepoint, 0x1dc0, 0x1dff) || in_range(codepoint, 0x20d0, 0x20ff) ||
        in_range(codepoint, 0xfe00, 0xfe0f) || in_range(codepoint, 0xfe20, 0xfe2f))
        return 0;
    if (in_range(codepoint, 0x1100, 0x115f) || in_range(codepoint, 0x2329, 0x232a) ||
        in_range(codepoint, 0x2e80, 0xa4cf) || in_range(codepoint, 0xac00, 0xd7a3) ||
        in_range(codepoint, 0xf900, 0xfaff) || in_range(codepoint, 0xfe10, 0xfe19) ||
        in_range(codepoint, 0xfe30, 0xfe6f) || in_range(codepoint, 0xff00, 0xff60) ||
        in_range(codepoint, 0xffe0, 0xffe6) || in_range(codepoint, 0x1f300, 0x1faff) ||
        in_range(codepoint, 0x20000, 0x3fffd))
        return 2;
    return 1;
}

bool terminal_utf8_append(std::string &output, uint32_t codepoint)
{
    if (codepoint > 0x10ffff || in_range(codepoint, 0xd800, 0xdfff))
        codepoint = kReplacement;
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    return true;
}
