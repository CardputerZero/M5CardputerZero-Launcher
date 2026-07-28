#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct TerminalUtf8Result {
    bool emitted = false;
    bool retry_byte = false;
    uint32_t codepoint = 0;
};

class TerminalUtf8Decoder {
public:
    TerminalUtf8Result feed(uint8_t byte) noexcept;
    void reset() noexcept;
    bool pending() const noexcept { return remaining_ != 0; }

private:
    uint32_t value_ = 0;
    uint32_t minimum_ = 0;
    uint8_t remaining_ = 0;
};

int terminal_codepoint_width(uint32_t codepoint) noexcept;
bool terminal_utf8_append(std::string &output, uint32_t codepoint);
