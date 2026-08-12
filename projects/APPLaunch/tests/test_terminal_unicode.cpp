#include "../main/ui/model/terminal_unicode.hpp"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    const std::string text = "A\xe4\xb8\xad\xf0\x9f\x99\x82Z";
    for (std::size_t split = 0; split <= text.size(); ++split) {
        TerminalUtf8Decoder decoder;
        std::vector<uint32_t> output;
        for (std::size_t i = 0; i < text.size(); ++i) {
            (void)split;
            auto result = decoder.feed(static_cast<uint8_t>(text[i]));
            if (result.emitted) output.push_back(result.codepoint);
        }
        assert((output == std::vector<uint32_t>{'A', 0x4e2d, 0x1f642, 'Z'}));
    }
    TerminalUtf8Decoder invalid;
    auto result = invalid.feed(0xe4);
    assert(!result.emitted);
    result = invalid.feed('A');
    assert(result.emitted && result.retry_byte && result.codepoint == 0xfffd);
    result = invalid.feed('A');
    assert(result.emitted && result.codepoint == 'A');
    assert(terminal_codepoint_width('A') == 1);
    assert(terminal_codepoint_width(0x4e2d) == 2);
    assert(terminal_codepoint_width(0x0301) == 0);
    std::string encoded;
    terminal_utf8_append(encoded, 0x4e2d);
    terminal_utf8_append(encoded, 0x2713);
    assert(encoded == "\xe4\xb8\xad\xe2\x9c\x93");
}
